import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";
import { execFileSync, spawn } from "node:child_process";

const appData = process.env.APPDATA;
if (!appData) {
  throw new Error("APPDATA is not set.");
}

const stamp = new Date().toISOString().replace(/[-:]/g, "").replace(/\..+/, "").replace("T", "-");
const scenePath = path.join(appData, "obs-studio", "basic", "scenes", "ç„¡é¡Œ.json");
const modulesPath = path.join(appData, "obs-studio", "plugin_manager", "modules.json");
const lockPath = path.join(appData, "obs-studio", ".dsk-cleanup.lock");
const transactionPath = path.join(appData, "obs-studio", ".dsk-cleanup-transaction.json");
const transactionId = `${stamp}-${process.pid}`;
const mutexName = `Global\\DSKObsCleanup-${createHash("sha256").update(path.resolve(appData).toLowerCase()).digest("hex").slice(0, 20)}`;

const disabledModules = [
  "aja-output-ui",
  "aja",
  "decklink-captions",
  "decklink-output-ui",
  "decklink",
  "AVerMediaCenter",
];

const args = process.argv.slice(2);
const supportedArgs = new Set(["--apply", "--dry-run"]);
const unknownArgs = args.filter((argument) => !supportedArgs.has(argument));
if (unknownArgs.length) {
  throw new Error(`Unknown argument(s): ${unknownArgs.join(", ")}. Use --dry-run or --apply.`);
}
if (args.includes("--apply") && args.includes("--dry-run")) {
  throw new Error("Choose either --dry-run or --apply, not both.");
}

// Safe by default: a configuration change always requires the explicit --apply flag.
const dryRun = !args.includes("--apply");
if (!dryRun && process.platform !== "win32") {
  throw new Error("Applying OBS configuration cleanup is supported only on Windows.");
}

function isObsRunning() {
  if (process.platform !== "win32") return false;

  try {
    const output = execFileSync(
      "tasklist.exe",
      ["/FI", "IMAGENAME eq obs64.exe", "/FO", "CSV", "/NH"],
      { encoding: "utf8", windowsHide: true },
    );
    return /"obs64\.exe"/i.test(output);
  } catch (error) {
    throw new Error(
      `Unable to determine whether OBS is running; refusing to edit its configuration. ${error.message}`,
    );
  }
}

function withCleanupDetail(error, cleanupErrors) {
  if (!cleanupErrors.length) return error;
  const message = error instanceof Error ? error.message : String(error);
  const combined = new Error(`${message} Cleanup also failed: ${cleanupErrors.join("; ")}`);
  combined.cause = error;
  return combined;
}

function removePathBestEffort(filePath, label, cleanupErrors) {
  try {
    if (fs.existsSync(filePath)) fs.rmSync(filePath, { force: true });
  } catch (error) {
    cleanupErrors.push(`${label}: ${error.message}`);
  }
}

function closeHandleBestEffort(handle, label, cleanupErrors) {
  if (handle === undefined) return;
  try {
    fs.closeSync(handle);
  } catch (error) {
    cleanupErrors.push(`${label}: ${error.message}`);
  }
}

function fsyncFile(filePath) {
  const handle = fs.openSync(filePath, "r+");
  try {
    fs.fsyncSync(handle);
  } finally {
    fs.closeSync(handle);
  }
}

function processIdIsRunning(pid) {
  try {
    const output = execFileSync(
      "tasklist.exe",
      ["/FI", `PID eq ${pid}`, "/FO", "CSV", "/NH"],
      { encoding: "utf8", windowsHide: true },
    );
    const expectedPid = String(pid);
    return output.split(/\r?\n/).some((line) => {
      const columns = line.match(/"(?:[^"]|"")*"/g) ?? [];
      return columns.length >= 2 && columns[1].slice(1, -1) === expectedPid;
    });
  } catch (error) {
    throw new Error(`Unable to verify the cleanup lock owner; refusing to reclaim the lock. ${error.message}`);
  }
}

function reclaimStaleApplyLock() {
  let lock;
  try {
    lock = readJson(lockPath);
  } catch (error) {
    if (error.code === "ENOENT")
      return { reclaimedStaleLock: false, removedArtifacts: [], retainedBackups: [] };
    throw new Error(`The cleanup lock is unreadable; refusing automatic removal: ${lockPath}. ${error.message}`);
  }
  const ownerPid = Number(lock.pid);
  const startedAtMs = Date.parse(String(lock.startedAt ?? ""));
  const staleTransactionId = String(lock.transactionId ?? "");
  if (!Number.isSafeInteger(ownerPid) || ownerPid <= 0 || !Number.isFinite(startedAtMs) ||
      !/^\d{8}-\d{6}-\d+$/.test(staleTransactionId)) {
    throw new Error(`The cleanup lock is invalid; refusing automatic removal: ${lockPath}`);
  }
  if (processIdIsRunning(ownerPid)) {
    throw new Error(`Another cleanup is running (PID ${ownerPid}), or its owner cannot yet be proven stopped: ${lockPath}`);
  }

  const stalePath = `${lockPath}.stale-${transactionId}`;
  try {
    fs.renameSync(lockPath, stalePath);
  } catch (error) {
    if (error.code === "ENOENT")
      return { reclaimedStaleLock: false, removedArtifacts: [], retainedBackups: [] };
    throw new Error(`Unable to claim the stale cleanup lock safely: ${error.message}`);
  }
  const cleanupErrors = [];
  removePathBestEffort(stalePath, "remove reclaimed stale cleanup lock", cleanupErrors);
  if (cleanupErrors.length)
    throw new Error(`The stale cleanup lock was reclaimed, but its quarantine file remains: ${cleanupErrors.join("; ")}`);

  const recovery = { reclaimedStaleLock: true, removedArtifacts: [], retainedBackups: [] };
  if (fs.existsSync(transactionPath)) return recovery;
  for (const filePath of [scenePath, modulesPath]) {
    const backupPath = `${filePath}.dsk-cleanup-backup-${staleTransactionId}`;
    const tempPath = `${filePath}.dsk-cleanup-temp-${staleTransactionId}`;
    const rollbackPath = `${filePath}.dsk-cleanup-rollback-${staleTransactionId}`;
    for (const [artifactPath, label] of [[tempPath, "temporary"], [rollbackPath, "rollback temporary"]]) {
      if (!fs.existsSync(artifactPath)) continue;
      removePathBestEffort(artifactPath, `remove stale ${label} file`, cleanupErrors);
      if (!fs.existsSync(artifactPath)) recovery.removedArtifacts.push(artifactPath);
    }
    if (!fs.existsSync(backupPath)) continue;
    if (fs.existsSync(filePath) && fileSha256(filePath) === fileSha256(backupPath)) {
      removePathBestEffort(backupPath, "remove redundant pre-transaction backup", cleanupErrors);
      if (!fs.existsSync(backupPath)) recovery.removedArtifacts.push(backupPath);
    } else {
      recovery.retainedBackups.push(backupPath);
    }
  }
  if (cleanupErrors.length)
    throw new Error(`The stale cleanup lock was reclaimed, but artifact cleanup failed: ${cleanupErrors.join("; ")}`);
  return recovery;
}

function acquireWindowsCleanupMutex() {
  const helperScript = [
    '$ErrorActionPreference = "Stop"',
    '$mutex = [Threading.Mutex]::new($false, $env:DSK_CLEANUP_MUTEX)',
    '$acquired = $false',
    'try {',
    '  try { $acquired = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired = $true }',
    '  if (-not $acquired) { [Console]::Out.WriteLine("BUSY"); [Console]::Out.Flush(); exit 3 }',
    '  [Console]::Out.WriteLine("READY"); [Console]::Out.Flush()',
    '  while ($true) { $line = [Console]::In.ReadLine(); if ($null -eq $line -or $line -eq "RELEASE") { break } }',
    '} finally {',
    '  if ($acquired) { $mutex.ReleaseMutex() }',
    '  $mutex.Dispose()',
    '}',
  ].join('; ');

  return new Promise((resolve, reject) => {
    const child = spawn(
      "powershell.exe",
      ["-NoProfile", "-NonInteractive", "-Command", helperScript],
      {
        env: { ...process.env, DSK_CLEANUP_MUTEX: mutexName },
        stdio: ["pipe", "pipe", "pipe"],
        windowsHide: true,
      },
    );
    let stdout = "";
    let stderr = "";
    let settled = false;
    const finish = (error) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      if (error) reject(error);
      else resolve(child);
    };
    const timeout = setTimeout(() => {
      child.kill();
      finish(new Error("Timed out while acquiring the Windows cleanup mutex."));
    }, 10000);
    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString("utf8");
      if (!stdout.includes("\n")) return;
      const status = stdout.trim();
      if (status.startsWith("READY")) finish();
      else if (status.startsWith("BUSY"))
        finish(new Error("Another OBS cleanup process is already running."));
      else
        finish(new Error(`Unexpected cleanup mutex response: ${status}`));
    });
    child.stderr.on("data", (chunk) => { stderr += chunk.toString("utf8"); });
    child.once("error", (error) => finish(new Error(`Unable to start the cleanup mutex helper: ${error.message}`)));
    child.once("exit", (code) => {
      if (!settled)
        finish(new Error(`Cleanup mutex helper exited before acquisition (code ${code}). ${stderr.trim()}`));
    });
  });
}

function releaseWindowsCleanupMutex(child) {
  return new Promise((resolve, reject) => {
    if (!child || child.exitCode !== null) {
      reject(new Error("The cleanup mutex helper exited unexpectedly before release."));
      return;
    }
    let stderr = "";
    let settled = false;
    const finish = (error) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      if (error) reject(error);
      else resolve();
    };
    const timeout = setTimeout(() => {
      child.kill();
      finish(new Error("Timed out while releasing the Windows cleanup mutex."));
    }, 10000);
    child.stderr.on("data", (chunk) => { stderr += chunk.toString("utf8"); });
    child.once("error", (error) => finish(error));
    child.once("exit", (code) => {
      if (code === 0) finish();
      else finish(new Error(`Cleanup mutex helper failed during release (code ${code}). ${stderr.trim()}`));
    });
    child.stdin.end("RELEASE\n");
  });
}

function withApplyLockFile(callback) {
  const staleLockRecovery = { reclaimedStaleLock: false, removedArtifacts: [], retainedBackups: [] };
  if (dryRun) return callback(staleLockRecovery);

  let lockHandle;
  try {
    for (let attempt = 0; attempt < 2; attempt += 1) {
      try {
        lockHandle = fs.openSync(lockPath, "wx");
        break;
      } catch (error) {
        if (error.code !== "EEXIST" || attempt > 0) throw error;
        const recovered = reclaimStaleApplyLock();
        staleLockRecovery.reclaimedStaleLock ||= recovered.reclaimedStaleLock;
        staleLockRecovery.removedArtifacts.push(...recovered.removedArtifacts);
        staleLockRecovery.retainedBackups.push(...recovered.retainedBackups);
      }
    }
    if (lockHandle === undefined)
      throw new Error(`Unable to acquire the cleanup lock: ${lockPath}`);
    fs.writeFileSync(
      lockHandle,
      `${JSON.stringify({ pid: process.pid, startedAt: new Date().toISOString(), transactionId })}${os.EOL}`,
      "utf8",
    );
    fs.fsyncSync(lockHandle);
  } catch (error) {
    const cleanupErrors = [];
    closeHandleBestEffort(lockHandle, "close partial cleanup lock", cleanupErrors);
    if (lockHandle !== undefined)
      removePathBestEffort(lockPath, "remove partial cleanup lock", cleanupErrors);
    if (error.code === "EEXIST" && lockHandle === undefined) {
      throw new Error(`Another cleanup acquired the lock while stale-lock recovery was in progress: ${lockPath}`);
    }
    throw withCleanupDetail(error, cleanupErrors);
  }

  let result;
  let callbackError;
  try {
    result = callback(staleLockRecovery);
  } catch (error) {
    callbackError = error;
  }

  const cleanupErrors = [];
  closeHandleBestEffort(lockHandle, "close cleanup lock", cleanupErrors);
  removePathBestEffort(lockPath, "remove cleanup lock", cleanupErrors);
  if (callbackError) throw withCleanupDetail(callbackError, cleanupErrors);
  if (cleanupErrors.length) {
    throw new Error(
      `OBS cleanup completed, but the cleanup lock could not be fully released: ${cleanupErrors.join("; ")}. ` +
      `Completed result: ${JSON.stringify(result)}`,
    );
  }
  return result;
}

async function withApplyLock(callback) {
  let mutexChild;
  if (process.platform === "win32")
    mutexChild = await acquireWindowsCleanupMutex();

  let result;
  let operationError;
  try {
    result = withApplyLockFile(callback);
  } catch (error) {
    operationError = error;
  }

  const cleanupErrors = [];
  if (mutexChild) {
    try {
      await releaseWindowsCleanupMutex(mutexChild);
    } catch (error) {
      cleanupErrors.push(`release Windows cleanup mutex: ${error.message}`);
    }
  }
  if (operationError) throw withCleanupDetail(operationError, cleanupErrors);
  if (cleanupErrors.length) {
    throw new Error(
      `OBS cleanup completed, but mutex release failed: ${cleanupErrors.join("; ")}. ` +
      `Completed result: ${JSON.stringify(result)}`,
    );
  }
  return result;
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function readJsonSnapshot(filePath) {
  const bytes = fs.readFileSync(filePath);
  return {
    value: JSON.parse(bytes.toString("utf8")),
    sha256: createHash("sha256").update(bytes).digest("hex"),
  };
}

function fileSha256(filePath) {
  return createHash("sha256").update(fs.readFileSync(filePath)).digest("hex");
}

function prepareJsonWrite(filePath, value, label, expectedSourceSha256) {
  const backupPath = `${filePath}.dsk-cleanup-backup-${transactionId}`;
  const tempPath = `${filePath}.dsk-cleanup-temp-${transactionId}`;
  const payload = `${JSON.stringify(value, null, 4)}${os.EOL}`;

  fs.copyFileSync(filePath, backupPath, fs.constants.COPYFILE_EXCL);
  try {
    fsyncFile(backupPath);
    const beforeSha256 = fileSha256(backupPath);
    if (beforeSha256 !== expectedSourceSha256)
      throw new Error(`${label} changed after it was read; refusing to prepare a stale update.`);
    const handle = fs.openSync(tempPath, "wx");
    try {
      fs.writeFileSync(handle, payload, "utf8");
      fs.fsyncSync(handle);
    } finally {
      fs.closeSync(handle);
    }
    JSON.parse(fs.readFileSync(tempPath, "utf8"));
  } catch (error) {
    const cleanupErrors = [];
    removePathBestEffort(tempPath, `remove temporary ${label} file`, cleanupErrors);
    removePathBestEffort(backupPath, `remove unused ${label} backup`, cleanupErrors);
    throw withCleanupDetail(error, cleanupErrors);
  }
  return {
    label,
    filePath,
    backupPath,
    tempPath,
    beforeSha256: expectedSourceSha256,
    afterSha256: fileSha256(tempPath),
  };
}

function restoreBackupAtomically(entry, expectedCurrentSha256) {
  const rollbackTempPath = `${entry.filePath}.dsk-cleanup-rollback-${transactionId}`;
  let restoreError;
  try {
    fs.copyFileSync(entry.backupPath, rollbackTempPath, fs.constants.COPYFILE_EXCL);
    fsyncFile(rollbackTempPath);
    JSON.parse(fs.readFileSync(rollbackTempPath, "utf8"));
    if (expectedCurrentSha256 &&
        (!fs.existsSync(entry.filePath) || fileSha256(entry.filePath) !== expectedCurrentSha256)) {
      throw new Error(`${entry.label} changed before rollback; refusing to overwrite the newer file.`);
    }
    fs.renameSync(rollbã‹h‘éì¶»§q«^tœÈH×NÂˆ™[[İ™T]™\İY™›Ü
›Û˜XÚÕ[\]™[[İ™H›Û˜XÚÈ[\Ü˜\Hš[H›Üˆ	Ù[K›X™[XÛX[\\œ›ÜœÊNÂˆYˆ
™\İÜ™Q\œ›ÜŠH›İÈÚ]ÛX[\]Z[
™\İÜ™Q\œ›Ü‹ÛX[\\œ›ÜœÊNÂˆYˆ
ÛX[\\œ›ÜœË›[™İ
Bˆ›İÈ™]È\œ›ÜŠ˜XÚİ\™\İÜ™HÛÛ\]Y›Üˆ	Ù[K›X™[K]ÛX[\˜Z[Yˆ	ØÛX[\\œ›ÜœËš›Ú[ŠÈŠ_X
NÂŸB‚™[˜İ[Ûˆ˜[Y]U˜[œØXİ[Û‘[J˜[YJHÂˆÛÛœİ[HHÂˆX™[ˆİš[™Ê˜[YOË›X™[ÏÈˆŠKˆš[T]ˆ]œ™\ÛÛ™Jİš[™Ê˜[YOË™š[T]ÏÈˆŠJKˆ˜XÚİ\]ˆ]œ™\ÛÛ™Jİš[™Ê˜[YOË˜˜XÚİ\]ÏÈˆŠJKˆ[\]ˆ]œ™\ÛÛ™Jİš[™Ê˜[YOË[\]ÏÈˆŠJKˆ™Y›Ü™TÚLMˆİš[™Ê˜[YOË˜™Y›Ü™TÚLMˆÏÈˆŠKÓİÙ\Ø\ÙJ
KˆY\”ÚLMˆİš[™Ê˜[YOË˜Y\”ÚLMˆÏÈˆŠKÓİÙ\Ø\ÙJ
KˆNÂˆÛÛœİ]Ù^HH
˜[YT]
HOˆ˜[YT]ÓİÙ\Ø\ÙJ
NÂˆÛÛœİ[İÙYš[\ÈH™]ÈÙ]
Ü]Ù^J]œ™\ÛÛ™JØÙ[™T]
JK]Ù^J]œ™\ÛÛ™J[Ù[\Ô]
JWJNÂˆÛÛœİš[S˜[YHH]˜˜\Ù[˜[YJ[K™š[T]
KÓİÙ\Ø\ÙJ
NÂˆÛÛœİØ[YQ\™XİÜHH]Ù^J]™\›˜[YJ[K™š[T]
JNÂˆYˆ
Y[K›X™[X[İÙYš[\Ëš\Ê]Ù^J[K™š[T]
JHˆ]Ù^J]™\›˜[YJ[K˜˜XÚİ\]
JHOOHØ[YQ\™XİÜHˆ]Ù^J]™\›˜[YJ[K[\]
JHOOHØ[YQ\™XİÜHˆ\]˜˜\Ù[˜[YJ[K˜˜XÚİ\]
KÓİÙ\Ø\ÙJ
Kœİ\ÕÚ]
	Ùš[S˜[Y_K™ÚËXÛX[\X˜XÚİ\X
Hˆ\]˜˜\Ù[˜[YJ[K[\]
KÓİÙ\Ø\ÙJ
Kœİ\ÕÚ]
	Ùš[S˜[Y_K™ÚËXÛX[\][\X
HˆK×–ÌNXKY—^ÍIË\İ
[K˜™Y›Ü™TÚLMŠHK×–ÌNXKY—^ÍIË\İ
[K˜Y\”ÚLMŠJHÂˆ›İÈ™]È\œ›ÜŠ’[\œ\YÛX[\›İ\›˜[ÛÛZ[œÈ[ˆ[˜[Y]È™Y\Ú[™È]]ÛX]XÈ™XÛİ™\KˆŠNÂˆBˆ™]\›ˆ[NÂŸB‚™[˜İ[ÛˆÜš]U˜[œØXİ[Û’›İ\›˜[
[šY\ÊHÂˆÛÛœİ›İ\›˜[[\]H	İ˜[œØXİ[Û”]K[\Iİ˜[œØXİ[Û’YXÂˆÛÛœİ^[ØYH	Ò”ÓÓ‹œİš[™ÚYJÂˆ™\œÚ[Ûˆ‹ˆÜ™X]Y]ˆ™]È]J
KÒTÓÔİš[™Ê
Kˆ[šY\Îˆ[šY\Ë›X\

ÈX™[š[T]˜XÚİ\][\]™Y›Ü™TÚLM‹Y\”ÚLMˆJHOˆ
ÂˆX™[ˆš[T]ˆ˜XÚİ\]ˆ[\]ˆ™Y›Ü™TÚLM‹ˆY\”ÚLM‹ˆJJKˆK[Š_IÛÜË‘SÓXÂˆYˆ
œË™^\İÔŞ[˜Ê˜[œØXİ[Û”]
JHÂˆ›İÈ™]È\œ›ÜŠ[ˆ[\œ\YÛX[\˜[œØXİ[Ûˆ]\İ™H™XÛİ™\™Yš\œİˆ	İ˜[œØXİ[Û”]X
NÂˆB‚ˆ]›İ\›˜[\œ›ÜÂˆHÂˆÛÛœİ[™HHœË›Ü[”Ş[˜Ê›İ\›˜[[\]ŞŠNÂˆHÂˆœËÜš]Qš[TŞ[˜Ê[™K^[ØY]ŠNÂˆœË™œŞ[˜ÔŞ[˜Ê[™JNÂˆHš[˜[HÂˆœË˜ÛÜÙTŞ[˜Ê[™JNÂˆBˆ”ÓÓ‹œ\œÙJœËœ™XYš[TŞ[˜Ê›İ\›˜[[\]]ŠJNÂˆœËœ™[˜[YTŞ[˜Ê›İ\›˜[[\]˜[œØXİ[Û”]
NÂˆœŞ[˜Ñš[J˜[œØXİ[Û”]
NÂˆHØ]Ú
\œ›ÜŠHÂˆ›İ\›˜[\œ›ÜˆH\œ›ÜÂˆBˆÛÛœİÛX[\\œ›ÜœÈH×NÂˆ™[[İ™T]™\İY™›Ü
›İ\›˜[[\]œ™[[İ™H˜[œØXİ[Ûˆ›İ\›˜[[\Ü˜\Hš[H‹ÛX[\\œ›ÜœÊNÂˆYˆ
›İ\›˜[\œ›ÜŠH›İÈÚ]ÛX[\]Z[
›İ\›˜[\œ›Ü‹ÛX[\\œ›ÜœÊNÂˆYˆ
ÛX[\\œ›ÜœË›[™İ
Bˆ›İÈ™]È\œ›ÜŠ˜[œØXİ[Ûˆ›İ\›˜[Ø\ÈÜš][‹][\Ü˜\HÛX[\˜Z[Yˆ	ØÛX[\\œ›ÜœËš›Ú[ŠÈŠ_X
NÂŸB‚™[˜İ[Ûˆ™XÛİ™\’[\œ\Y˜[œØXİ[ÛŠ
HÂˆYˆ
YœË™^\İÔŞ[˜Ê˜[œØXİ[Û”]
JH™]\›ˆ×NÂ‚ˆÛÛœİ›İ\›˜[H™XYœÛÛŠ˜[œØXİ[Û”]
NÂˆYˆ
›İ\›˜[™\œÚ[ÛˆOOHˆP\œ˜^Kš\Ğ\œ˜^J›İ\›˜[™[šY\ÊH›İ\›˜[™[šY\Ë›[™İOOH
Bˆ›İÈ™]È\œ›ÜŠ[\œ\YÛX[\›İ\›˜[\È[˜[Yˆ	İ˜[œØXİ[Û”]X
NÂˆÛÛœİ[šY\ÈH›İ\›˜[™[šY\Ë›X\
˜[Y]U˜[œØXİ[Û‘[JNÂˆÛÛœİ\™Ù]Ù^\ÈH[šY\Ë›X\

[JHOˆ[K™š[T]ÓİÙ\Ø\ÙJ
JNÂˆYˆ
™]ÈÙ]
\™Ù]Ù^\ÊKœÚ^™HOOH\™Ù]Ù^\Ë›[™İ
Bˆ›İÈ™]È\œ›ÜŠ’[\œ\YÛX[\›İ\›˜[ÛÛZ[œÈ\XØ]H\™Ù]š[\ÎÈ™Y\Ú[™È]]ÛX]XÈ™XÛİ™\KˆŠNÂ‚ˆÛÛœİ˜[Y][Û‘\œ›ÜœÈH×NÂˆ›Üˆ
ÛÛœİ[HÙˆ[šY\ÊHÂˆHÂˆYˆ
YœË™^\İÔŞ[˜Ê[K˜˜XÚİ\]
JBˆ›İÈ™]È\œ›ÜŠ˜XÚİ\Z\ÜÚ[™Îˆ	Ù[K˜˜XÚİ\]X
NÂˆYˆ
š[TÚLMŠ[K˜˜XÚİ\]
HOOH[K˜™Y›Ü™TÚLMŠBˆ›İÈ™]È\œ›ÜŠ˜XÚİ\\ÚÚ[™ÙYˆ	Ù[K˜˜XÚİ\]X
NÂˆYˆ
YœË™^\İÔŞ[˜Ê[K™š[T]
JBˆ›İÈ™]È\œ›ÜŠ\™Ù]Z\ÜÚ[™Îˆ	Ù[K™š[T]X
NÂˆÛÛœİİ\œ™[ÚLMˆHš[TÚLMŠ[K™š[T]
NÂˆYˆ
İ\œ™[ÚLMˆOOH[K˜™Y›Ü™TÚLMˆ	‰ˆİ\œ™[ÚLMˆOOH[K˜Y\”ÚLMŠBˆ›İÈ™]È\œ›ÜŠ\™Ù]Ú[™ÙYİ]ÚYHH[\œ\Y˜[œØXİ[Ûˆ	Ù[K™š[T]X
NÂˆHØ]Ú
\œ›ÜŠHÂˆ˜[Y][Û‘\œ›ÜœËœ\Ú
	Ù[K›X™[Nˆ	Ù\œ›Ü‹›Y\ÜØYÙ_X
NÂˆBˆBˆYˆ
˜[Y][Û‘\œ›ÜœË›[™İ
HÂˆ›İÈ™]È\œ›ÜŠ[\œ\YĞ”ÈÛX[\™YYÈX[X[™\ÛÛ][ÛÈ›Èš[\ÈÙ\™H™\İÜ™Yˆ	İ˜[Y][Û‘\œ›ÜœËš›Ú[ŠÈŠ_X
NÂˆB‚ˆÛÛœİ™\İÜ™Q\œ›ÜœÈH×NÂˆ›Üˆ
ÛÛœİ[HÙˆË‹‹™[šY\×Kœ™]™\œÙJ
JHÂˆHÂˆÛÛœİİ\œ™[ÚLMˆHš[TÚLMŠ[K™š[T]
NÂˆYˆ
İ\œ™[ÚLMˆOOH[K˜Y\”ÚLMŠBˆ™\İÜ™P˜XÚİ\]ÛZXØ[J[K[K˜Y\”ÚLMŠNÂˆ[ÙHYˆ
İ\œ™[ÚLMˆOOH[K˜™Y›Ü™TÚLMŠBˆ›İÈ™]È\œ›ÜŠ\™Ù]Ú[™ÙY\š[™È™XÛİ™\Nˆ	Ù[K™š[T]X
NÂˆHØ]Ú
\œ›ÜŠHÂˆ™\İÜ™Q\œ›ÜœËœ\Ú
	Ù[K›X™[Nˆ	Ù\œ›Ü‹›Y\ÜØYÙ_X
NÂˆBˆBˆYˆ
™\İÜ™Q\œ›ÜœË›[™İ
HÂˆ›İÈ™]È\œ›ÜŠ[\œ\YĞ”ÈÛX[\™XÛİ™\H˜Z[YÈH›İ\›˜[Ø\È™]Z[™Yˆ	Ü™\İÜ™Q\œ›ÜœËš›Ú[ŠÈŠ_X
NÂˆB‚ˆÛÛœİÛX[\\œ›ÜœÈH×NÂˆ›Üˆ
ÛÛœİ[HÙˆ[šY\ÊBˆ™[[İ™T]™\İY™›Ü
[K[\]™[[İ™H[\œ\Y[\Ü˜\Hš[H›Üˆ	Ù[K›X™[XÛX[\\œ›ÜœÊNÂˆ™[[İ™T]™\İY™›Ü
˜[œØXİ[Û”]œ™[[İ™H™XÛİ™\™Y˜[œØXİ[Ûˆ›İ\›˜[‹ÛX[\\œ›ÜœÊNÂˆYˆ
ÛX[\\œ›ÜœË›[™İ
Bˆ›İÈ™]È\œ›ÜŠ[\œ\YĞ”ÈÛX[\Ø\È™\İÜ™Y]™XÛİ™\HÛX[\˜Z[Yˆ	ØÛX[\\œ›ÜœËš›Ú[ŠÈŠ_X
NÂˆ™]\›ˆ[šY\ÎÂŸB‚™[˜İ[Ûˆ˜[Y]T™\\™Y[J[JHÂˆYˆ
YœË™^\İÔŞ[˜Ê[K™š[T]
Hš[TÚLMŠ[K™š[T]
HOOH[K˜™Y›Ü™TÚLMŠBˆ›İÈ™]È\œ›ÜŠ	Ù[K›X™[HÚ[™ÙYÚ[HÛX[\Ø\È™Z[™È™\\™YÈ™Y\Ú[™ÈÈİ™\Üš]H]˜
NÂˆYˆ
YœË™^\İÔŞ[˜Ê[K˜˜XÚİ\]
Hš[TÚLMŠ[K˜˜XÚİ\]
HOOH[K˜™Y›Ü™TÚLMŠBˆ›İÈ™]È\œ›ÜŠ	Ù[K›X™[H˜XÚİ\›ÈÛ™Ù\ˆX]Ú\ÈHÛİ\˜ÙHÛ˜\Úİ˜
NÂˆYˆ
YœË™^\İÔŞ[˜Ê[K[\]
Hš[TÚLMŠ[K[\]
HOOH[K˜Y\”ÚLMŠBˆ›İÈ™]È\œ›ÜŠ	Ù[K›X™[H[\Ü˜\H\]H›ÈÛ™Ù\ˆX]Ú\ÈH™\\™Y^[ØY˜
NÂŸB‚™[˜İ[ÛˆÛÛ[Z]œÛÛ•Üš]\Ê[šY\ÊHÂˆYˆ
Y[šY\Ë›[™İ
H™]\›ÂˆÛÛœİÛÛ[Z]YH×NÂˆ]ÛÛ[Z]\œ›ÜÂˆHÂˆ›Üˆ
ÛÛœİ[HÙˆ[šY\ÊBˆ˜[Y]T™\\™Y[J[JNÂˆÜš]U˜[œØXİ[Û’›İ\›˜[
[šY\ÊNÂˆ›Üˆ
ÛÛœİ[HÙˆ[šY\ÊHÂˆ˜[Y]T™\\™Y[J[JNÂˆœËœ™[˜[YTŞ[˜Ê[K[\][K™š[T]
NÂˆÛÛ[Z]Yœ\Ú
[JNÂˆœŞ[˜Ñš[J[K™š[T]
NÂˆBˆœËœ›TŞ[˜Ê˜[œØXİ[Û”]
NÂˆHØ]Ú
\œ›ÜŠHÂˆÛÛ[Z]\œ›ÜˆH\œ›ÜÂˆB‚ˆÛÛœİÛX[\\œ›ÜœÈH×NÂˆYˆ
ÛÛ[Z]\œ›ÜŠHÂˆÛÛœİ™\İÜ™Q\œ›ÜœÈH×NÂˆ›Üˆ
ÛÛœİ[HÙˆÛÛ[Z]Yœ™]™\œÙJ
JHÂˆHÂˆYˆ
YœË™^\İÔŞ[˜Ê[K™š[T]
JBˆ›İÈ™]È\œ›ÜŠ\™Ù]\ÈZ\ÜÚ[™ÈY\ˆÛÛ[Z]ŠNÂˆÛÛœİİ\œ™[ÚLMˆHš[TÚLMŠ[K™š[T]
NÂˆYˆ
İ\œ™[ÚLMˆOOH[K˜Y\”ÚLMŠBˆ™\İÜ™P˜XÚİ\]ÛZXØ[J[K[K˜Y\”ÚLMŠNÂˆ[ÙHYˆ
İ\œ™[ÚLMˆOOH[K˜™Y›Ü™TÚLMŠBˆ›İÈ™]È\œ›ÜŠ\™Ù]Ú[™ÙYY\ˆÛÛ[Z]È]]ÛX]XÈ›Û˜XÚÈØ\ÈÚÚ\YÈ™\Ù\™HH™]Ù\ˆš[HŠNÂˆHØ]Ú
™\İÜ™Q\œ›ÜŠHÂˆ™\İÜ™Q\œ›ÜœËœ\Ú
	Ù[K›X™[Nˆ	Ü™\İÜ™Q\œ›Ü‹›Y\ÜØYÙ_X
NÂˆBˆBˆYˆ
\™\İÜ™Q\œ›ÜœË›[™İ
Bˆ™[[İ™T]™\İY™›Ü
˜[œØXİ[Û”]œ™[[İ™H›ÛYX˜XÚÈ˜[œØXİ[Ûˆ›İ\›˜[‹ÛX[\\œ›ÜœÊNÂˆÛÛœİ™\İÜ™Q]Z[H™\İÜ™Q\œ›ÜœË›[™İˆÈ™\İÜ™H[ÛÈ˜Z[Y›Üˆ	Ü™\İÜ™Q\œ›ÜœËš›Ú[ŠÈŠ_XˆˆˆÜšYÚ[˜[š[\ÈÙ\™H™\İÜ™Yœ›ÛH˜XÚİ\ËˆÂˆÛÛœİ˜XÚİ\]Z[H˜XÚİ\Îˆ	Ù[šY\Ë›X\

[JHOˆ[K˜˜XÚİ\]
Kš›Ú[ŠÈŠ_K˜ÂˆÛÛ[Z]\œ›ÜˆH™]È\œ›ÜŠ]ÛZXÈĞ”ÈÛÛ™šYİ\˜][Ûˆ\]H˜Z[Y‰Ü™\İÜ™Q]Z[IØ˜XÚİ\]Z[H	ØÛÛ[Z]\œ›Ü‹›Y\ÜØYÙ_X
NÂˆB‚ˆ›Üˆ
ÛÛœİ[HÙˆ[šY\ÊBˆ™[[İ™T]™\İY™›Ü
[K[\]™[[İ™H[\Ü˜\Hš[H›Üˆ	Ù[K›X™[XÛX[\\œ›ÜœÊNÂˆYˆ
ÛÛ[Z]\œ›ÜŠH›İÈÚ]ÛX[\]Z[
ÛÛ[Z]\œ›Ü‹ÛX[\\œ›ÜœÊNÂˆYˆ
ÛX[\\œ›ÜœË›[™İ
Bˆ›İÈ™]È\œ›ÜŠĞ”ÈÛÛ™šYİ\˜][ÛˆØ\ÈÛÛ[Z]Y][\Ü˜\HÛX[\˜Z[Yˆ	ØÛX[\\œ›ÜœËš›Ú[ŠÈŠ_X
NÂŸB‚™[˜İ[Ûˆ\œ˜^SÙŠ˜[YJHÂˆYˆ
]˜[YJH™]\›ˆ×NÂˆ™]\›ˆ\œ˜^Kš\Ğ\œ˜^J˜[YJHÈ˜[YHˆİ˜[YWNÂŸB‚™[˜İ[Ûˆ\Ù\™›\YÓÜ[ÛŠÜ[ÛœËÙ^K˜[YJHÂˆÛÛœİ[œ]Hİš[™ÊÜ[ÛœÈÏÈˆŠNÂˆÛÛœİ˜[™Ù\ÈH×NÂˆ][™^HÂˆÚ[H
[™^[œ]›[™İ
HÂˆÚ[H
[™^[œ]›[™İ	‰ˆ×ËË\İ
[œ]Ú[™^JJH[™^
ÏHNÂˆYˆ
[™^H[œ]›[™İ
Hœ™XZÎÂˆÛÛœİİ\H[™^Âˆ]][İHHˆÂˆ]\ØØ\YH˜[ÙNÂˆÚ[H
[™^[œ]›[™İ
HÂˆÛÛœİÚ\˜Xİ\ˆH[œ]Ú[™^NÂˆYˆ
\ØØ\Y
HÂˆ\ØØ\YH˜[ÙNÂˆH[ÙHYˆ
Ú\˜Xİ\ˆOOH—ŠHÂˆ\ØØ\YHYNÂˆH[ÙHYˆ
][İJHÂˆYˆ
Ú\˜Xİ\ˆOOH][İJH][İHHˆÂˆH[ÙHYˆ
Ú\˜Xİ\ˆOOH	È‰ÈÚ\˜Xİ\ˆOOH‰ÈŠHÂˆ][İHHÚ\˜Xİ\ÂˆH[ÙHYˆ
×ËË\İ
Ú\˜Xİ\ŠJHÂˆœ™XZÎÂˆBˆ[™^
ÏHNÂˆBˆ˜[™Ù\Ëœ\Ú
Èİ\[™ˆ[™^JNÂˆB‚ˆÛÛœİX]Ú[™ÈH˜[™Ù\Ë™š[\Š
Èİ\[™JHOˆÂˆÛÛœİÚÙ[ˆH[œ]œÛXÙJİ\[™
NÂˆÛÛœİÙ\\˜]ÜˆHÚÙ[‹š[™^ÙŠHŠNÂˆ™]\›ˆÙ\\˜]Üˆˆ	‰ˆÚÙ[‹œÛXÙJÙ\\˜]ÜŠHOOHÙ^NÂˆJNÂˆYˆ
[X]Ú[™Ë›[™İ
HÂˆYˆ
Z[œ]
H™]\›ˆ	ÚÙ^_OIİ˜[Y_XÂˆ™]\›ˆ	Ú[œ]IË×ÉË\İ
[œ]
HÈˆˆˆˆŸIÚÙ^_OIİ˜[Y_XÂˆB‚ˆ]™\İ[HˆÂˆ]İ\œÛÜˆHÂˆ›Üˆ
ÛÛœİÈİ\[™HÙˆX]Ú[™ÊHÂˆ™\İ[
ÏH[œ]œÛXÙJİ\œÛÜ‹İ\
NÂˆ™\İ[
ÏH	ÚÙ^_OIİ˜[Y_XÂˆİ\œÛÜˆH[™ÂˆBˆ™]\›ˆ™\İ[
È[œ]œÛXÙJİ\œÛÜŠNÂŸB‚™[˜İ[ÛˆÛX[\ØÙ[™JØÙ[™JHÂˆÛÛœİÚ[™Ù\ÈH×NÂ‚ˆ›Üˆ
ÛÛœİÛİ\˜ÙHÙˆ\œ˜^SÙŠØÙ[™KœÛİ\˜Ù\ÊJHÂˆYˆ
Ûİ\˜ÙOËšYOOH™Úİ×Ú[œ]ŠHÂˆÛİ\˜ÙKœÙ][™ÜÈÏÏHßNÂˆYˆ
Ûİ\˜ÙKœÙ][™ÜË™XXİ]˜]WİÚ[—Û›İÜÚİÚ[™ÈOOHYJHÂˆÛİ\˜ÙKœÙ][™ÜË™XXİ]˜]WİÚ[—Û›İÜÚİÚ[™ÈHYNÂˆÚ[™Ù\Ëœ\Ú
ÚİÈÛİ\˜ÙH	ÉÜÛİ\˜ÙK›˜[Y_IÈÚ[XXİ]˜]HÚ[ˆ›İÚİÚ[™Ø
NÂˆBˆB‚ˆÛÛœİÜ[œ]HÛİ\˜ÙOËšYOOH™™›\Y×ÜÛİ\˜ÙHˆÈİš[™ÊÛİ\˜ÙKœÙ][™ÜÏËš[œ]ÏÈˆŠHˆˆÂˆÛÛœİ\ÓYØXŞTØÚÙ]ÈHÜ[œ]OOHœÜ‹ËÌNL‹ŒMŒLŒLMMÛ]™KÜØÚÙ]ÈÂˆÛÛœİ\ÒÛYPØ[Y\˜HH×œÜ—×ÌNL—ŒMŒLŒLMVÍKNWÚÛYWØØ[VÌKMIË\İ
Ü[œ]
NÂˆYˆ
\ÓYØXŞTØÚÙ]È\ÒÛYPØ[Y\˜JHÂˆYˆ
Ûİ\˜ÙKœÙ][™ÜË˜ÛÜÙWİÚ[—Ú[˜Xİ]™HOOHYJHÂˆÛİ\˜ÙKœÙ][™ÜË˜ÛÜÙWİÚ[—Ú[˜Xİ]™HHYNÂˆÚ[™Ù\Ëœ\Ú
•ÔYYXHÛİ\˜ÙH	ÉÜÛİ\˜ÙK›˜[Y_IÈÚ[ÛÜÙHÚ[ˆ[˜Xİ]™X
NÂˆBˆYˆ
Ûİ\˜ÙKœÙ][™ÜËœ™\İ\ÛÛ—ØXİ]˜]HOOHYJHÂˆÛİ\˜ÙKœÙ][™ÜËœ™\İ\ÛÛ—ØXİ]˜]HHYNÂˆÚ[™Ù\Ëœ\Ú
•ÔYYXHÛİ\˜ÙH	ÉÜÛİ\˜ÙK›˜[Y_IÈÚ[™\İ\Û›HÚ[ˆXİ]˜]Y
NÂˆBˆYˆ
\ÒÛYPØ[Y\˜JHÂˆÛÛœİ™›\YÓÜ[ÛœÈH\Ù\™›\YÓÜ[ÛŠÛİ\˜ÙKœÙ][™ÜË™™›\Y×ÛÜ[ÛœËœÜİ˜[œÜÜ‹ÜŠNÂˆYˆ
Ûİ\˜ÙKœÙ][™ÜË™™›\Y×ÛÜ[ÛœÈOOH™›\YÓÜ[ÛœÊHÂˆÛİ\˜ÙKœÙ][™ÜË™™›\Y×ÛÜ[ÛœÈH™›\YÓÜ[ÛœÎÂˆÚ[™Ù\Ëœ\Ú
•ÔYYXHÛİ\˜ÙH	ÉÜÛİ\˜ÙK›˜[Y_IÈÚ[\ÙHÔ˜[œÜÜ
NÂˆBˆBˆB‚ˆYˆ
Ûİ\˜ÙOË™š[\œÊHÂˆÛÛœİš[\œÈH\œ˜^SÙŠÛİ\˜ÙK™š[\œÊNÂˆÛÛœİÙ\Hš[\œË™š[\Š
š[\ŠHOˆJš[\ËšYOOH›šYXWØ]Y[ÙÙš[\ˆˆ	‰ˆš[\‹™[˜X›YOOH˜[ÙJJNÂˆYˆ
Ù\›[™İOOHš[\œË›[™İ
HÂˆÚ[™Ù\Ëœ\Ú
™[[İ™Y\ØX›Y•’QPH]Y[ÈY™™XİÈš[\ˆœ›ÛH	ÉÜÛİ\˜ÙK›˜[Y_IØ
NÂˆYˆ
Ù\›[™İˆ
HÂˆÛİ\˜ÙK™š[\œÈHÙ\ÂˆH[ÙHÂˆ[]HÛİ\˜ÙK™š[\œÎÂˆBˆBˆBˆB‚ˆ™]\›ˆÚ[™Ù\ÎÂŸB‚™[˜İ[ÛˆÛX[\[Ù[\Ê[Ù[\ÊHÂˆÛÛœİÚ[™Ù\ÈH×NÂˆÛÛœİS˜[YHH™]ÈX\
[Ù[\Ë›X\

[Ù[JHOˆÛ[Ù[K›[Ù[WÛ˜[YK[Ù[WJJNÂ‚ˆ›Üˆ
ÛÛœİ[Ù[S˜[YHÙˆ\ØX›Y[Ù[\ÊHÂˆÛÛœİ^\İ[™ÈHS˜[YK™Ù]
[Ù[S˜[YJNÂˆYˆ
^\İ[™ÊHÂˆYˆ
^\İ[™Ë™[˜X›YOOH˜[ÙJHÂˆ^\İ[™Ë™[˜X›YH˜[ÙNÂˆÚ[™Ù\Ëœ\Ú
\ØX›Y[\ÙYĞ”È[Ù[H	ÉÛ[Ù[S˜[Y_IØ
NÂˆBˆÛÛ[YNÂˆB‚ˆ[Ù[\Ëœ\Ú
Âˆ\Ü^WÛ˜[YNˆˆ‹ˆ[˜X›Yˆ˜[ÙKˆ[˜ÛÙ\œÎˆ×KˆYˆˆ‹ˆ[Ù[WÛ˜[YNˆ[Ù[S˜[YKˆİ]]Îˆ×KˆÙ\šXÙ\Îˆ×KˆÛİ\˜Ù\Îˆ×Kˆ™\œÚ[Ûˆˆ‹ˆJNÂˆÚ[™Ù\Ëœ\Ú
YY\ØX›Y[H›Üˆ[\ÙYĞ”È[Ù[H	ÉÛ[Ù[S˜[Y_IØ
NÂˆB‚ˆ™]\›ˆÚ[™Ù\ÎÂŸB‚˜ÛÛœİ™\ÜH]ØZ]Ú]\SØÚÊ
İ[SØÚÔ™XÛİ™\JHOˆÂˆYˆ
T[ˆ	‰ˆœË™^\İÔŞ[˜Ê˜[œØXİ[Û”]
JHÂˆ›İÈ™]È\œ›ÜŠ[ˆ[\œ\YÛX[\˜[œØXİ[Ûˆ\È[™[™ËˆÛÜÙHĞ”È[™[ˆÚ]KX\HÈ™XÛİ™\ˆ]ˆ	İ˜[œØXİ[Û”]X
NÂˆBˆYˆ
YT[ˆ	‰ˆ\ÓØœÔ[›š[™Ê
JHÂˆ›İÈ™]È\œ›ÜŠ“Ğ”È\È[›š[™ËˆÛÜÙHĞ”È™Y›Ü™H\Z[™ÈØÙ[™HÛX[\Üˆ\ÙHKYK\[ˆÈ[œÜXİÚ[™Ù\ËˆŠNÂˆBˆÛÛœİ™XÛİ™\™Y[šY\ÈHT[ˆÈ×Hˆ™XÛİ™\’[\œ\Y˜[œØXİ[ÛŠ
NÂ‚ˆÛÛœİØÙ[™TÛ˜\ÚİH™XYœÛÛ”Û˜\Úİ
ØÙ[™T]
NÂˆÛÛœİ[Ù[\ÔÛ˜\ÚİH™XYœÛÛ”Û˜\Úİ
[Ù[\Ô]
NÂˆÛÛœİØÙ[™HHØÙ[™TÛ˜\Úİ˜[YNÂˆÛÛœİ[Ù[\ÈH[Ù[\ÔÛ˜\Úİ˜[YNÂˆÛÛœİØÙ[™PÚ[™Ù\ÈHÛX[\ØÙ[™JØÙ[™JNÂˆÛÛœİ[Ù[PÚ[™Ù\ÈHÛX[\[Ù[\Ê[Ù[\ÊNÂ‚ˆÛÛœİ™\\™YÜš]\ÈH×NÂˆYˆ
YT[ŠHÂˆ]ÛÛ[Z]İ\YH˜[ÙNÂˆHÂˆYˆ
ØÙ[™PÚ[™Ù\Ë›[™İ
Bˆ™\\™YÜš]\Ëœ\Ú
™\\™RœÛÛ•Üš]JØÙ[™T]ØÙ[™KœØÙ[™H‹ØÙ[™TÛ˜\ÚİœÚLMŠJNÂˆYˆ
[Ù[PÚ[™Ù\Ë›[™İ
Bˆ™\\™YÜš]\Ëœ\Ú
™\\™RœÛÛ•Üš]J[Ù[\Ô][Ù[\Ë›[Ù[\È‹[Ù[\ÔÛ˜\ÚİœÚLMŠJNÂˆYˆ
™\\™YÜš]\Ë›[™İ	‰ˆ\ÓØœÔ[›š[™Ê
JHÂˆ›İÈ™]È\œ›ÜŠ“Ğ”Èİ\YÚ[HÛX[\Ø\È™Z[™È™\\™YÈ›ÈÛÛ™šYİ\˜][ÛˆÚ[™Ù\ÈÙ\™HÛÛ[Z]YˆŠNÂˆBˆÛÛ[Z]İ\YHYNÂˆÛÛ[Z]œÛÛ•Üš]\Ê™\\™YÜš]\ÊNÂˆHØ]Ú
\œ›ÜŠHÂˆÛÛœİÛX[\\œ›ÜœÈH×NÂˆ›Üˆ
ÛÛœİ[HÙˆ™\\™YÜš]\ÊHÂˆ™[[İ™T]™\İY™›Ü
[K[\]™[[İ™H[˜ÛÛ[Z]Y[\Ü˜\Hš[H›Üˆ	Ù[K›X™[XÛX[\\œ›ÜœÊNÂˆYˆ
XÛÛ[Z]İ\Y
Bˆ™[[İ™T]™\İY™›Ü
[K˜˜XÚİ\]™[[İ™H[\ÙY˜XÚİ\›Üˆ	Ù[K›X™[XÛX[\\œ›ÜœÊNÂˆBˆ›İÈÚ]ÛX[\]Z[
\œ›Ü‹ÛX[\\œ›ÜœÊNÂˆBˆB‚ˆÛÛœİ˜XÚİ\ÈH™\\™YÜš]\Ë›X\

ÈX™[š[T]˜XÚİ\]JHOˆ
ÈX™[š[T]˜XÚİ\]JJNÂˆ™]\›ˆÂˆØÙ[™T]ˆ[Ù[\Ô]ˆT[‹ˆ™XÛİ™\™Y[\œ\Y˜[œØXİ[Ûˆ™XÛİ™\™Y[šY\Ë›[™İˆˆ™XÛİ™\™Y˜[œØXİ[Û˜XÚİ\Îˆ™XÛİ™\™Y[šY\Ë›X\

ÈX™[˜XÚİ\]JHOˆ
ÈX™[˜XÚİ\]JJKˆ™XÛZ[YYİ[SØÚÎˆİ[SØÚÔ™XÛİ™\Kœ™XÛZ[YYİ[SØÚËˆ™[[İ™Yİ[P\Y˜XİÎˆİ[SØÚÔ™XÛİ™\Kœ™[[İ™Y\Y˜XİËˆ™]Z[™Yİ[P˜XÚİ\Îˆİ[SØÚÔ™XÛİ™\Kœ™]Z[™Y˜XÚİ\ËˆÚ[™Ù\ÎˆË‹‹œØÙ[™PÚ[™Ù\Ë‹‹›[Ù[PÚ[™Ù\×Kˆ˜XÚİ\ËˆNÂŸJNÂ‚˜ÛÛœÛÛK›ÙÊˆ”ÓÓ‹œİš[™ÚYJˆ™\Üˆ[ˆ‹ˆ
KŠNÂ