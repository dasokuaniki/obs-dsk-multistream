import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const appData = process.env.APPDATA;
if (!appData) {
  throw new Error("APPDATA is not set.");
}

const stamp = new Date().toISOString().replace(/[-:]/g, "").replace(/\..+/, "").replace("T", "-");
const scenePath = path.join(appData, "obs-studio", "basic", "scenes", "無題.json");
const modulesPath = path.join(appData, "obs-studio", "plugin_manager", "modules.json");

const disabledModules = [
  "aja-output-ui",
  "aja",
  "decklink-captions",
  "decklink-output-ui",
  "decklink",
  "AVerMediaCenter",
];

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function writeJsonWithBackup(filePath, value, label) {
  const backupPath = `${filePath}.dsk-cleanup-backup-${stamp}`;
  fs.copyFileSync(filePath, backupPath);
  fs.writeFileSync(filePath, `${JSON.stringify(value, null, 4)}${os.EOL}`, "utf8");
  return { label, filePath, backupPath };
}

function arrayOf(value) {
  if (!value) return [];
  return Array.isArray(value) ? value : [value];
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

    if (source?.id === "ffmpeg_source" && source.settings?.input === "rtsp://192.168.100.10:8554/live/pocket3") {
      if (source.settings.close_when_inactive !== true) {
        source.settings.close_when_inactive = true;
        changes.push(`RTSP media source '${source.name}' will close when inactive`);
      }
      if (source.settings.restart_on_activate !== true) {
        source.settings.restart_on_activate = true;
        changes.push(`RTSP media source '${source.name}' will restart only when activated`);
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

const scene = readJson(scenePath);
const modules = readJson(modulesPath);
const sceneChanges = cleanupScene(scene);
const moduleChanges = cleanupModules(modules);

const backups = [];
if (sceneChanges.length) {
  backups.push(writeJsonWithBackup(scenePath, scene, "scene"));
}
if (moduleChanges.length) {
  backups.push(writeJsonWithBackup(modulesPath, modules, "modules"));
}

console.log(
  JSON.stringify(
    {
      scenePath,
      modulesPath,
      changes: [...sceneChanges, ...moduleChanges],
      backups,
    },
    null,
    2,
  ),
);
