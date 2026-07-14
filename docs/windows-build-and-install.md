# Windows Build and Install

This project targets OBS Studio 32.x. As of 2026-06-02, the current OBS documentation site is for OBS Studio 32.1.2.

Official references:

- OBS developer docs: https://docs.obsproject.com/
- OBS plugin template: https://github.com/obsproject/obs-plugintemplate
- OBS Windows build instructions: https://obsproject.com/wiki/build-instructions-for-windows

## Required Tools

- Visual Studio Build Tools with the MSVC C++ toolchain.
- CMake.
- Ninja or a Visual Studio CMake generator.
- Qt 6 matching the OBS build.
- OBS development files that provide:
  - `libobsConfig.cmake`
  - `obs-frontend-apiConfig.cmake`
- OBS dependency files that provide `CURLConfig.cmake`; DSK uses OBS' signed libcurl runtime for OAuth and platform HTTPS APIs.

The runtime OBS installer alone is not enough for building this plugin.

## Preflight Check

Run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\check-prereqs.ps1
```

If OBS or Qt is installed outside common locations, pass prefixes:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\check-prereqs.ps1 -ObsPrefix C:\path\to\obs-sdk -ObsDepsPrefix C:\path\to\obs-deps -QtPrefix C:\path\to\obs-deps-qt6
```

For the local OBS 32.1.2 SDK prepared in this workspace, use:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\check-prereqs.ps1 -ObsPrefix deps\obs-sdk -ObsDepsPrefix deps\obs-studio-32.1.2\.deps\obs-deps-2025-08-23-x64 -QtPrefix deps\obs-studio-32.1.2\.deps\obs-deps-qt6-2025-08-23-x64
```

## Configure

Run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\configure-windows.ps1 -ObsPrefix C:\path\to\obs-sdk -ObsDepsPrefix C:\path\to\obs-deps -QtPrefix C:\path\to\obs-deps-qt6 -OAuthAppConfig C:\private\oauth-app-config.json
```

`OAuthAppConfig` is the publisher-only build input for one-click YouTube login. It is not installed as a separate file and must have this shape:

```json
{
  "youtube": {
    "clientId": "publisher-desktop-client.apps.googleusercontent.com",
    "clientSecret": "publisher-desktop-client-secret"
  }
}
```

The configure step validates the values and generates `build\windows-x64-sdk3\generated\oauth-publisher-config.hpp`. Keep both the JSON and generated build tree out of source control and release archives. If the argument is omitted in this workspace, `scripts\configure-windows.ps1` detects the existing DSK Comment Viewer publisher config. Use `-DisableBundledOAuth` to explicitly produce a build with no publisher config; that build remains compatible but requires `Use custom Google OAuth app` for YouTube login.

Then build:

```powershell
cmake --build build\windows-x64-sdk3 --parallel
ctest --test-dir build\windows-x64-sdk3 --output-on-failure
```

If `cmake` is not on PATH, use the CMake path printed by `scripts\check-prereqs.ps1`.

For the local OBS 32.1.2 SDK prepared in this workspace, use:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\configure-windows.ps1 -ObsPrefix deps\obs-sdk -ObsDepsPrefix deps\obs-studio-32.1.2\.deps\obs-deps-2025-08-23-x64 -QtPrefix deps\obs-studio-32.1.2\.deps\obs-deps-qt6-2025-08-23-x64 -BuildDir build\windows-x64-sdk3
cmake --build build\windows-x64-sdk3 --parallel
ctest --test-dir build\windows-x64-sdk3 --output-on-failure
```

`DSK_ENABLE_OBS_CANVAS_API` defaults to `ON` because this project targets OBS 32.x. Use `-DisableObsCanvasApi` only when testing against an older or incompatible OBS development tree.

## Install for Local Testing

After building, install into the OBS user plugin folder:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\install-user-plugin.ps1 -BuildDir build\windows-x64-sdk3
```

The script copies the plugin binary and data directory into:

```text
%ProgramData%\obs-studio\plugins\obs-dsk-multistream\bin\64bit\
%ProgramData%\obs-studio\plugins\obs-dsk-multistream\data\
```

Expected files:

```text
bin\64bit\obs-dsk-multistream.dll
data\locale\en-US.ini
data\locale\ja-JP.ini
data\presets\platforms.json
```

No Qt TLS plugin DLLs are bundled. OAuth and YouTube/Kick/Twitch API requests use the signed `libcurl.dll` shipped in the OBS runtime.

Restart OBS. The plugin registers these docks:

- `DSK Streaming` with `Routes`, `Controls`, `Scenes`, and `Activity` tabs
- `DSK Vertical` with an always-visible preview and optional Setup controls
- `DSK Comments`
- `DSK Comments` (browser dock backed by DSK Comment Viewer)

## Automated Checks

After install, validate the package layout and data files:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\validate-package.ps1 -BuildDir build\windows-x64-sdk3
```

The script checks:

- Built DLL exists.
- Installed DLL exists and matches the built DLL after normalizing the PE signature area.
- Platform presets are valid JSON and include Twitch, YouTube, Kick, TikTok, and Custom RTMP.
- `en-US` and `ja-JP` locale files have matching keys.
- The OBS runtime contains a valid Authenticode-signed `libcurl.dll`, and the DSK package contains no obsolete Qt TLS plugins.

To verify local RTMP send/receive plumbing without touching external streaming services, install `ffmpeg.exe` on PATH or place a portable build under `deps\ffmpeg-portable`, then run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\run-local-rtmp-smoke.ps1
```

The script starts an `ffmpeg -listen 1` RTMP receiver on localhost, publishes a generated 1280x720 test stream into it, and checks both logs for accepted media streams and transmitted frames.

To run the OBS-integrated E2E checks, close any running OBS instance, install the current build, then run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\install-user-plugin.ps1 -BuildDir build\windows-x64-sdk3
powershell.exe -ExecutionPolicy Bypass -File scripts\run-obs-rtmp-e2e.ps1 -EncoderGroup dsk-horizontal
powershell.exe -ExecutionPolicy Bypass -File scripts\run-obs-rtmp-e2e.ps1 -EncoderGroup dsk-vertical
powershell.exe -ExecutionPolicy Bypass -File scripts\run-obs-rtmp-e2e.ps1 -EncoderGroup dsk-horizontal -SecondEncoderGroup dsk-vertical -SecondPort 19454
powershell.exe -ExecutionPolicy Bypass -File scripts\run-dsk-loop-e2e.ps1 -Iterations 1
```

This starts one or two local RTMP receivers, creates a lightweight `DSK E2E` OBS profile and scene collection, launches OBS with `DSK_E2E_AUTORUN=1`, creates non-persistent runtime targets, starts DSK outputs, verifies received H.264/AAC frames, checks the expected output resolution (`1920x1080` for horizontal, `1080x1920` for vertical), and checks OBS logs for DSK start/stop lines. The E2E targets are not saved to the OBS profile.

For stable automation, the E2E script temporarily disables known external OBS plugins that can block or crash headless startup on this machine, including `vertical-canvas`, `aitum-multistream`, `obs-asio`, `StreamDeckPlugin`, and `AVerMediaCenter`. It restores `%APPDATA%\obs-studio\plugin_manager\modules.json` in `finally`, and also restores a previous `.dsk-e2e.bak` backup at the next run if an earlier test process was killed.

## Current Local Test Result

On the current machine, the Windows build and install-layout checks pass against OBS Studio 32.1.2 development files:

- `scripts\check-prereqs.ps1`: passed with local OBS SDK, obs-deps, and Qt prefixes.
- CMake configure: passed with `DSK_ENABLE_OBS_CANVAS_API=ON`.
- Plugin build: passed and produced `build\windows-x64-sdk3\obs-dsk-multistream.dll`.
- CTest smoke tests: passed for both normal OBS 32 canvas builds and `-DisableObsCanvasApi` fallback builds.
- Local RTMP smoke test: passed with a portable BtbN FFmpeg build under `deps\ffmpeg-portable`.
- OBS-integrated RTMP E2E: passed for `DSK Horizontal`, `DSK Vertical`, dual horizontal, horizontal plus vertical, vertical plus horizontal, and looped mixed-output runs using the script-generated `DSK E2E` OBS profile/scene collection; received streams were verified at `1920x1080` and `1080x1920`.
- DLL dependency check: depends on OBS/Qt/libcurl/VC runtime DLLs expected to exist in the OBS runtime.
- User-plugin layout check: passed under `build\install-test\obs-dsk-multistream`.
- OBS runtime load check: passed. The OBS 32.1.2 log contains `[DSK Multistream] Loaded` and `obs-dsk-multistream.dll` in `Loaded Modules`.
- Package validation: passed for the previously installed build, 5 platform presets, and matching locale keys. Re-run it after each installation; signed PE files are compared after normalizing the signature area.
- Fallback build with `-DisableObsCanvasApi`: passed.

Final visual verification still requires opening OBS on Windows and checking the `DSK Streaming` and `DSK Vertical` docks.

## Application Control and release signing

The no-OBS test executables and development plugin DLL are unsigned by default. Windows Defender Application Control or Smart App Control can block those files before their test code starts. A `BAD_COMMAND` result together with Code Integrity event IDs `3033` or `3077` is an execution-policy block, not a failed test assertion.

Run the read-only diagnostic to distinguish an application failure from a policy block:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\diagnose-first-run.ps1 -OutputPath build\dsk-first-run-diagnostic.json
```

Release packages intended for other machines should be Authenticode signed with the publisher's approved code-signing certificate. After signing both the built DLL and staged package DLL, enforce the release gate with:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\validate-package.ps1 -BuildDir build\windows-x64-sdk3 -ObsPluginRoot path\to\staged\obs-dsk-multistream -ObsPluginScanRoot path\to\staged\obs-dsk-multistream -RequireValidSignature
```
