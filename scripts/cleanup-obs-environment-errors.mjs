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
const scenePath = path.join(appData, "obs-studio", "basic", "scenes", "無題.json");
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
    fs.renameSync(rollbackTempPath, entry.filePath);
    fsyncFile(entry.filePath);
  } catch (error) {
    restoreError = error;
  }
  const cleanupErrors = [];
  removePathBestEffort(rollbackTempPath, `remove rollback temporary file for ${entry.label}`, cleanupErrors);
  if (restoreError) throw withCleanupDetail(restoreError, cleanupErrors);
  if (cleanupErrors.length)
    throw new Error(`Backup restore completed for ${entry.label}, but cleanup failed: ${cleanupErrors.join("; ")}`);
}

function validateTransactionEntry(value) {
  const entry = {
    label: String(value?.label ?? ""),
    filePath: path.resolve(String(value?.filePath ?? "")),
    backupPath: path.resolve(String(value?.backupPath ?? "")),
    tempPath: path.resolve(String(value?.tempPath ?? "")),
    beforeSha256: String(value?.beforeSha256 ?? "").toLowerCase(),
    afterSha256: String(value?.afterSha256 ?? "").toLowerCase(),
  };
  const pathKey = (valuePath) => valuePath.toLowerCase();
  const allowedFiles = new Set([pathKey(path.resolve(scenePath)), pathKey(path.resolve(modulesPath))]);
  const fileName = path.basename(entry.filePath).toLowerCase();
  const sameDirectory = pathKey(path.dirname(entry.filePath));
  if (!entry.label || !allowedFiles.has(pathKey(entry.filePath)) ||
      pathKey(path.dirname(entry.backupPath)) !== sameDirectory ||
      pathKey(path.dirname(entry.tempPath)) !== sameDirectory ||
      !path.basename(entry.backupPath).toLowerCase().startsWith(`${fileName}.dsk-cleanup-backup-`) ||
      !path.basename(entry.tempPath).toLowerCase().startsWith(`${fileName}.dsk-cleanup-temp-`) ||
      !/^[0-9a-f]{64}$/.test(entry.beforeSha256) || !/^[0-9a-f]{64}$/.test(entry.afterSha256)) {
    throw new Error("Interrupted cleanup journal contains an invalid path; refusing automatic recovery.");
  }
  return entry;
}

function writeTransactionJournal(entries) {
  const journalTempPath = `${transactionPath}.temp-${transactionId}`;
  const payload = `${JSON.stringify({
    version: 2,
    createdAt: new Date().toISOString(),
    entries: entries.map(({ label, filePath, backupPath, tempPath, beforeSha256, afterSha256 }) => ({
      label,
      filePath,
      backupPath,
      tempPath,
      beforeSha256,
      afterSha256,
    })),
  }, null, 2)}${os.EOL}`;
  if (fs.existsSync(transactionPath)) {
    throw new Error(`An interrupted cleanup transaction must be recovered first: ${transactionPath}`);
  }

  let journalError;
  try {
    const handle = fs.openSync(journalTempPath, "wx");
    try {
      fs.writeFileSync(handle, payload, "utf8");
      fs.fsyncSync(handle);
    } finally {
      fs.closeSync(handle);
    }
    JSON.parse(fs.readFileSync(journalTempPath, "utf8"));
    fs.renameSync(journalTempPath, transactionPath);
    fsyncFile(transactionPath);
  } catch (error) {
    journalError = error;
  }
  const cleanupErrors = [];
  removePathBestEffort(journalTempPath, "remove transaction journal temporary file", cleanupErrors);
  if (journalError) throw withCleanupDetail(journalError, cleanupErrors);
  if (cleanupErrors.length)
    throw new Error(`Transaction journal was written, but temporary cleanup failed: ${cleanupErrors.join("; ")}`);
}

function recoverInterruptedTransaction() {
  if (!fs.existsSync(transactionPath)) return [];

  const journal = readJson(transactionPath);
  if (journal.version !== 2 || !Array.isArray(journal.entries) || journal.entries.length === 0)
    throw new Error(`Interrupted cleanup journal is invalid: ${transactionPath}`);
  const entries = journal.entries.map(validateTransactionEntry);
  const targetKeys = entries.map((entry) => entry.filePath.toLowerCase());
  if (new Set(targetKeys).size !== targetKeys.length)
    throw new Error("Interrupted cleanup journal contains duplicate target files; refusing automatic recovery.");

  const validationErrors = [];
  for (const entry of entries) {
    try {
      if (!fs.existsSync(entry.backupPath))
        throw new Error(`backup missing: ${entry.backupPath}`);
      if (fileSha256(entry.backupPath) !== entry.beforeSha256)
        throw new Error(`backup hash changed: ${entry.backupPath}`);
      if (!fs.existsSync(entry.filePath))
        throw new Error(`target missing: ${entry.filePath}`);
      const currentSha256 = fileSha256(entry.filePath);
      if (currentSha256 !== entry.beforeSha256 && currentSha256 !== entry.afterSha256)
        throw new Error(`target changed outside the interrupted transaction: ${entry.filePath}`);
    } catch (error) {
      validationErrors.push(`${entry.label}: ${error.message}`);
    }
  }
  if (validationErrors.length) {
    throw new Error(`Interrupted OBS cleanup needs manual resolution; no files were restored. ${validationErrors.join("; ")}`);
  }

  const restoreErrors = [];
  for (const entry of [...entries].reverse()) {
    try {
      const currentSha256 = fileSha256(entry.filePath);
      if (currentSha256 === entry.afterSha256)
        restoreBackupAtomically(entry, entry.afterSha256);
      else if (currentSha256 !== entry.beforeSha256)
        throw new Error(`target changed during recovery: ${entry.filePath}`);
    } catch (error) {
      restoreErrors.push(`${entry.label}: ${error.message}`);
    }
  }
  if (restoreErrors.length) {
    throw new Error(`Interrupted OBS cleanup recovery failed; the journal was retained. ${restoreErrors.join("; ")}`);
  }

  const cleanupErrors = [];
  for (const entry of entries)
    removePathBestEffort(entry.tempPath, `remove interrupted temporary file for ${entry.label}`, cleanupErrors);
  removePathBestEffort(transactionPath, "remove recovered transaction journal", cleanupErrors);
  if (cleanupErrors.length)
    throw new Error(`Interrupted OBS cleanup was restored, but recovery cleanup failed: ${cleanupErrors.join("; ")}`);
  return entries;
}

function validatePreparedEntry(entry) {
  if (!fs.existsSync(entry.filePath) || fileSha256(entry.filePath) !== entry.beforeSha256)
    throw new Error(`${entry.label} changed while cleanup was being prepared; refusing to overwrite it.`);
  if (!fs.existsSync(entry.backupPath) || fileSha256(entry.backupPath) !== entry.beforeSha256)
    throw new Error(`${entry.label} backup no longer matches the source snapshot.`);
  if (!fs.existsSync(entry.tempPath) || fileSha256(entry.tempPath) !== entry.afterSha256)
    throw new Error(`${entry.label} temporary update no longer matches the prepared payload.`);
}

function commitJsonWrites(entries) {
  if (!entries.length) return;
  const committed = [];
  let commitError;
  try {
    for (const entry of entries)
      validatePreparedEntry(entry);
    writeTransactionJournal(entries);
    for (const entry of entries) {
      validatePreparedEntry(entry);
      fs.renameSync(entry.tempPath, entry.filePath);
      committed.push(entry);
      fsyncFile(entry.filePath);
    }
    fs.rmSync(transactionPath);
  } catch (error) {
    commitError = error;
  }

  const cleanupErrors = [];
  if (commitError) {
    const restoreErrors = [];
    for (const entry of committed.reverse()) {
      try {
        if (!fs.existsSync(entry.filePath))
          throw new Error("target is missing after commit");
        const currentSha256 = fileSha256(entry.filePath);
        if (currentSha256 === entry.afterSha256)
          restoreBackupAtomically(entry, entry.afterSha256);
        else if (currentSha256 !== entry.beforeSha256)
          throw new Error("target changed after commit; automatic rollback was skipped to preserve the newer file");
      } catch (restoreError) {
        restoreErrors.push(`${entry.label}: ${restoreError.message}`);
      }
    }
    if (!restoreErrors.length)
      removePathBestEffort(transactionPath, "remove rolled-back transaction journal", cleanupErrors);
    const restoreDetail = restoreErrors.length
      ? ` Restore also failed for ${restoreErrors.join("; ")}`
      : " Original files were restored from backups.";
    const backupDetail = ` Backups: ${entries.map((entry) => entry.backupPath).join("; ")}.`;
    commitError = new Error(`Atomic OBS configuration update failed.${restoreDetail}${backupDetail} ${commitError.message}`);
  }

  for (const entry of entries)
    removePathBestEffort(entry.tempPath, `remove temporary file for ${entry.label}`, cleanupErrors);
  if (commitError) throw withCleanupDetail(commitError, cleanupErrors);
  if (cleanupErrors.length)
    throw new Error(`OBS configuration was committed, but temporary cleanup failed: ${cleanupErrors.join("; ")}`);
}

function arrayOf(value) {
  if (!value) return [];
  return Array.isArray(value) ? value : [value];
}

function upsertFfmpegOption(options, key, value) {
  const input = String(options ?? "");
  const ranges = [];
  let index = 0;
  while (index < input.length) {
    while (index < input.length && /\s/.test(input[index])) index += 1;
    if (index >= input.length) break;
    const start = index;
    let quote = "";
    let escaped = false;
    while (index < input.length) {
      const character = input[index];
      if (escaped) {
        escaped = false;
      } else if (character === "\\") {
        escaped = true;
      } else if (quote) {
        if (character === quote) quote = "";
      } else if (character === '"' || character === "'") {
        quote = character;
      } else if (/\s/.test(character)) {
        break;
      }
      index += 1;
    }
    ranges.push({ start, end: index });
  }

  const matching = ranges.filter(({ start, end }) => {
    const token = input.slice(start, end);
    const separator = token.indexOf("=");
    return separator > 0 && token.slice(0, separator) === key;
  });
  if (!matching.length) {
    if (!input) return `${key}=${value}`;
    return `${input}${/\s$/.test(input) ? "" : " "}${key}=${value}`;
  }

  let result = "";
  let cursor = 0;
  for (const { start, end } of matching) {
    result += input.slice(cursor, start);
    result += `${key}=${value}`;
    cursor = end;
  }
  return result + input.slice(cursor);
}

function cleanupScene(scene) {
  const changes = [];

  for (const source of arrayOf(scene.sources)) {
    if (source?.id === "dshow_input") {
      source.settings ??= {};
      if (source.settings.deactivate_when_not_showing !== true) {
        source.settings.deactivate_when_not_showing = true;
        changes.push(`DShow source '${source.name}' will deactivate when not showing`);
      }
    }

    const rtspInput = source?.id === "ffmpeg_source" ? String(source.settings?.input ?? "") : "";
    const isLegacyPocket3 = rtspInput === "rtsp://192.168.100.10:8554/live/pocket3";
    const isHomeCamera = /^rtsp:\/\/192\.168\.100\.10:855[5-8]\/home\/cam[1-4]$/.test(rtspInput);
    if (isLegacyPocket3 || isHomeCamera) {
      if (source.settings.close_when_inactive !== true) {
        source.settings.close_when_inactive = true;
        changes.push(`RTSP media source '${source.name}' will close when inactive`);
      }
      if (source.settings.restart_on_activate !== true) {
        source.settings.restart_on_activate = true;
        changes.push(`RTSP media source '${source.name}' will restart only when activated`);
      }
      if (isHomeCamera) {
        const ffmpegOptions = upsertFfmpegOption(source.settings.ffmpeg_options, "rtsp_transport", "tcp");
        if (source.settings.ffmpeg_options !== ffmpegOptions) {
          source.settings.ffmpeg_options = ffmpegOptions;
          changes.push(`RTSP media source '${source.name}' will use TCP transport`);
        }
      }
    }

    if (source?.filters) {
      const filters = arrayOf(source.filters);
      const kept = filters.filter((filter) => !(filter?.id === "nvidia_audiofx_filter" && filter.enabled === false));
      if (kept.length !== filters.length) {
        changes.push(`Removed disabled NVIDIA Audio Effects filter from '${source.name}'`);
        if (kept.length > 0) {
          source.filters = kept;
        } else {
          delete source.filters;
        }
      }
    }
  }

  return changes;
}

function cleanupModules(modules) {
  const changes = [];
  const byName = new Map(modules.map((module) => [module.module_name, module]));

  for (const moduleName of disabledModules) {
    const existing = byName.get(moduleName);
    if (existing) {
      if (existing.enabled !== false) {
        existing.enabled = false;
        changes.push(`Disabled unused OBS module '${moduleName}'`);
      }
      continue;
    }

    modules.push({
      display_name: "",
      enabled: false,
      encoders: [],
      id: "",
      module_name: moduleName,
      outputs: [],
      services: [],
      sources: [],
      version: "",
    });
    changes.push(`Added disabled entry for unused OBS module '${moduleName}'`);
  }

  return changes;
}

const report = await withApplyLock((staleLockRecovery) => {
  if (dryRun && fs.existsSync(transactionPath)) {
    throw new Error(`An interrupted cleanup transaction is pending. Close OBS and run with --apply to recover it: ${transactionPath}`);
  }
  if (!dryRun && isObsRunning()) {
    throw new Error("OBS is running. Close OBS before applying scene cleanup, or use --dry-run to inspect changes.");
  }
  const recoveredEntries = dryRun ? [] : recoverInterruptedTransaction();

  const sceneSnapshot = readJsonSnapshot(scenePath);
  const modulesSnapshot = readJsonSnapshot(modulesPath);
  const scene = sceneSnapshot.value;
  const modules = modulesSnapshot.value;
  const sceneChanges = cleanupScene(scene);
  const moduleChanges = cleanupModules(modules);

  const preparedWrites = [];
  if (!dryRun) {
    let commitStarted = false;
    try {
      if (sceneChanges.length)
        preparedWrites.push(prepareJsonWrite(scenePath, scene, "scene", sceneSnapshot.sha256));
      if (moduleChanges.length)
        preparedWrites.push(prepareJsonWrite(modulesPath, modules, "modules", modulesSnapshot.sha256));
      if (preparedWrites.length && isObsRunning()) {
        throw new Error("OBS started while cleanup was being prepared; no configuration changes were committed.");
      }
      commitStarted = true;
      commitJsonWrites(preparedWrites);
    } catch (error) {
      const cleanupErrors = [];
      for (const entry of preparedWrites) {
        removePathBestEffort(entry.tempPath, `remove uncommitted temporary file for ${entry.label}`, cleanupErrors);
        if (!commitStarted)
          removePathBestEffort(entry.backupPath, `remove unused backup for ${entry.label}`, cleanupErrors);
      }
      throw withCleanupDetail(error, cleanupErrors);
    }
  }

  const backups = preparedWrites.map(({ label, filePath, backupPath }) => ({ label, filePath, backupPath }));
  return {
    scenePath,
    modulesPath,
    dryRun,
    recoveredInterruptedTransaction: recoveredEntries.length > 0,
    recoveredTransactionBackups: recoveredEntries.map(({ label, backupPath }) => ({ label, backupPath })),
    reclaimedStaleLock: staleLockRecovery.reclaimedStaleLock,
    removedStaleArtifacts: staleLockRecovery.removedArtifacts,
    retainedStaleBackups: staleLockRecovery.retainedBackups,
    changes: [...sceneChanges, ...moduleChanges],
    backups,
  };
});

console.log(
  JSON.stringify(
    report,
    null,
    2,
  ),
);
