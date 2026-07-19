# Competitive Research

This project is GPL-2.0-or-later, so GPL-compatible OBS plugin code can be studied and, when needed, incorporated with attribution. Prefer copying proven OBS API patterns over copying large source blocks.

## Reviewed Projects

- `sorayuki/obs-multi-rtmp`: https://github.com/sorayuki/obs-multi-rtmp
  - License: GPL-2.0.
  - Useful patterns: per-target output objects, profile-local JSON config, reusable encoder configuration IDs, main streaming/recording encoder placeholders, OBS frontend start/stop sync, delayed-output handling, and output cleanup that avoids detaching encoders/services while an output is active.
- `Aitum/obs-aitum-multistream`: https://github.com/Aitum/obs-aitum-multistream
  - License: GPL-2.0.
  - Useful patterns: one dock grouped by main canvas and vertical canvas, platform icon inference from endpoint URL, advanced encoder settings that stay hidden until enabled, custom resolution lists for horizontal and vertical outputs, and integration hooks for Aitum Vertical.
- `Aitum/obs-vertical-canvas`: https://github.com/Aitum/obs-vertical-canvas
  - License: GPL-2.0.
  - Useful patterns: extra canvas lifecycle, vertical stream output list, per-output start/stop, stream-output serialization with `obs_data_array_t`, hotkey persistence, OBS canvas scene tracking, and dock-driven vertical layout workflow.

## Adopted Now

- DSK output cleanup now follows the same safe pattern as `obs-multi-rtmp`: stop the output first, only detach encoders and service after the output is inactive, and release encoder references acquired from the output.
- DSK E2E automation now isolates unstable third-party OBS plugins during headless startup and restores plugin-manager state afterward.
- E2E startup now queues a direct OBS UI-task start; this avoided flaky Qt timer firing during vertical-primary mixed-output runs while keeping listener startup isolated in the script.

## Good Next Candidates

- Add asynchronous stop-session cleanup based on output stop signals, so normal UI Stop can remain graceful while still avoiding active-output detach.
- Add per-target status counters similar to `obs-multi-rtmp`: duration, bytes, bitrate, frames, and dropped frames.
- Add Aitum-style platform icon inference from RTMP endpoint URL, especially for Custom RTMP targets.
- Expand encoder groups into reusable named profiles, with optional source scene/canvas selection per profile.
- Add an Advanced encoder panel that is collapsed by default and exposes encoder selection, frame-rate divisor, scale filter, resolution, and audio track only when needed.
- Add hotkeys for OBS-driven route toggles and per-target enable/disable, following Aitum's hotkey persistence style.
- Move vertical layout persistence closer to OBS canvas scene save/load semantics so the vertical dock feels native.
