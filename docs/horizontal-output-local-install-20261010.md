# Independent horizontal output — local development installation

Date: 2026-10-10 JST. Authorization: install on the current Codex PC for the user's own testing. Not a public release; no external streaming or GAME PC deployment is authorized by this installation.

## Candidate

- Build: `build/horizontal-install-20261010`.
- Source: HEAD `be916fa8ce8e1b0a9a8e3404b9398c2734ea4d55` plus the current uncommitted feature changes.
- Source-file fingerprint (116 files in src/data/cmake/tests plus CMakeLists): `CD298F6F76E4A0CCC721A9C03BFB4A43CB291243ED8C198724C4CAC38EB7D565`.
- DLL: 2,424,320 bytes; SHA-256 `D45605C5EE6656C4D3BF170787D7A1F3C2281D35F8D027BA5BD4706E6DF64DE9`.
- E2E hooks OFF; publisher-release OFF; existing approved Multistream publisher OAuth configuration retained without changing scopes, redirects, or credential ownership.
- File metadata 0.4.1, but **unsigned development build**, not the signed public 0.4.1.
- Full Windows build and CTest 28/28 passed. Staged package validation passed. Defender custom scan of the staged payload completed with exit 0 and no threats.

## Backup and recovery boundary

Before installation, OBS process count was zero. GPU preflight: stopped, Light available, 44 degrees C, 6,616 MiB free VRAM.

Backup: `C:\Users\dasok\AppData\Local\DSK\Multistream\Backups\horizontal-dev-20261010-165901`.

The backup contains the dedicated old developer-plugin directory (9 files) and a relative `obs-config` tree (25 files: DSK profile data, global.ini, and plugin-manager state). All copies were hash-checked. It is private local recovery data and must not be uploaded, committed, or copied into distribution artifacts.

The target is only `C:\ProgramData\obs-studio\plugins\obs-dsk-multistream-dev`; the DLL is named `obs-dsk-multistream-dev.dll`, preserving the existing developer module identity. The separate installed distribution plugin and its registration are not replaced.

For rollback, first confirm all OBS stream/record/virtual-camera/DSK outputs are stopped, then exit OBS normally. Preserve the test configuration before any restoration. Restore only the backed-up developer-plugin directory and the corresponding pre-test DSK profile files. Schema-2 settings must not be loaded or edited as an old schema by the previous DLL. Restore global.ini/plugin-manager state only when the test actually changed them; do not overwrite intervening user work. Do not delete unrelated profiles, scenes, or credentials.

## User path after verified startup

1. OBS **Docks → DSK Horizontal**. Enable Setup when editing, create the horizontal scene and add/reuse OBS sources.
2. Keep OBS Link OFF for scene switching independent of OBS Main.
3. **DSK Streaming → gear → Targets → Edit** the bilibili target.
4. Set Output direction to **DSK Horizontal (16:9)**; use Manual RTMP with the URL/key issued to the user by bilibili.
5. Preview the independent composition before starting. The preview is pre-encode composition, not confirmation of reception by bilibili.

## Deployment verification

Installed atomically into the existing dedicated developer-plugin directory after a fresh OBS-stopped check. All 8 installed package files match the manifest; the installed DLL hash matches the candidate above. The separate distribution-plugin directory was left unchanged.

OBS 33.0.0-beta5 was started normally once. The running process loaded the new developer DLL. Native UI inspection confirmed **Docks → DSK Horizontal** and the visible independent horizontal dock, initially showing Default with 0 sources and a black 16:9 preview. The startup log `2026-10-10 17-02-29.txt` lists the developer module with no observed module-loading failure or crash. OBS is left open with the dock visible for the user's test; no stream, RTMP transmission, or target-key change was performed. Normal shutdown and uninstall after this local deployment have not been tested.

Remaining feature acceptance: real platform delivery, hardware/audio/AV sync, long-duration behavior, all editing/collection/encoder-sharing paths and origin of the single allocation reported in the earlier isolated canary. Public signing/release/install/update/uninstall gates remain separate.
