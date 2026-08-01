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

The configure step validates the values and generates `build\windows-x64-sdk3\generated\oauth-publisher-config.hpp`. Keep both the JSON and generated build tree out of source control and release archives. The config path must be supplied explicitly with `-OAuthAppConfig` or `DSK_OAUTH_APP_CONFIG`; the build never searches neighboring workspaces for credentials. Use `-DisableBundledOAuth` to explicitly produce a build with no publisher config; that build remains compatible but requires `Use custom Google OAuth app` for YouTube login.

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

- `DSK Streaming` with `Controls` as the main page and `Targets` in the gear menu
- `DSK Vertical` with an always-visible preview and optional Setup controls
- `DSK Comments` (optional browser dock; Multistream enables the integration automatically when the separate Viewer is installed, then detects/starts it and validates its local v1 API)

Scene Routing is retained for internal testing but hidden from normal beta users. Set `DSK_EXPERIMENTAL_SCENE_ROUTING=1` before launching OBS to expose its settings and gear-menu page.

## Build the Distribution Installer

Install Inno Setup 7, then build the plugin and run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\build-windows-installer.ps1 -BuildDir build\windows-x64-sdk3
```

The packaging script rejects build trees or DLLs that contain E2E automation hooks. If this build directory was used for OBS-integrated E2E testing, run the normal configure command again without `-EnableE2eHooks` and rebuild before packaging.

The script stages only the runtime DLL, locale files, platform presets, and a SHA-256 manifest. It validates the staged layout, rejects development artifacts, compiles the installer without warnings, and writes:

```text
release\DSK-Multistream-<version>-Windows-x64-Setup.exe
release\DSK-Multistream-<version>-Windows-x64-Setup.exe.sha256
```

The installer requires Windows 10 or later, 64-bit OBS Studio 32 or later, and administrator approval because it installs for all users under `%ProgramData%\obs-studio\plugins\obs-dsk-multistream`. Close OBS before installing or uninstalling. A newer package with the same application ID performs an in-place update.

Uninstall from **Windows Settings > Apps > Installed apps > DSK Multistream for OBS > Uninstall**. Uninstall removes the dedicated plugin directory and Windows uninstall registration. OBS profiles, DSK target/layout settings, stream keys, and OAuth credentials are stored outside that directory and are intentionally preserved so reinstalling does not erase user configuration.

Run the isolated install/update/uninstall test with an E2E-only installer build:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\build-windows-installer.ps1 -BuildDir build\windows-x64-sdk3 -OutputDir build\installer-e2e-artifacts -InstallerE2E
powershell.exe -ExecutionPolicy Bypass -File scripts\test-windows-installer.ps1 -InstallerPath build\installer-e2e-artifacts\DSK-Multistream-<version>-Windows-x64-E2E-Setup.exe
```

The E2E package uses a separate application ID and isolated install directory. It must not be distributed. The test verifies fresh install, same-ID update, uninstall registration, complete plugin-directory removal, and preservation of data outside the plugin directory.

## Automated Checks

After install, validate the package layout and data files:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\validate-package.ps1 -BuildDir build\windows-x64-sdk3
```

The script checks:

- Built DLL exists.
- Installed DLL exists and matches the built DLL after normalizing the PE signature area.
- Platform presets are valid JSON and include Twitch, YouTube, Kick, and Manual RTMP.
- `en-US` and `ja-JP` locale files have matching keys.
- The OBS runtime contains a valid Authenticode-signed `libcurl.dll`, and the DSK package contains no obsolete Qt TLS plugins.

To verify local RTMP send/receive plumbing without touching external streaming services, install `ffmpeg.exe` on PATH or place a portable build under `deps\ffmpeg-portable`, then run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\run-local-rtmp-smoke.ps1
```

The script starts an `ffmpeg -listen 1` RTMP receiver on localhost, publishes a generated 1280x720 test stream into it, and checks both logs for accepted media streams and transmitted frames.

To run the OBS-integrated E2E checks, close any running OBS instance, install the current build, then run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\configure-windows.ps1 -BuildDir build\windows-x64-sdk3 -EnableE2eHooks
cmake --build build\windows-x64-sdk3
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

Release packages intended for other machines are built by `.github/workflows/windows-release.yml`. The workflow downloads pinned OBS dependencies, builds the plugin, runs tests, and creates the installer on a GitHub-hosted Windows runner.

For SignPath release signing, configure these repository values after the SignPath Foundation application is approved:

- Secret: `SIGNPATH_API_TOKEN`
- Variable: `SIGNPATH_ORGANIZATION_ID`
- Variable: `SIGNPATH_PROJECT_SLUG`
- Variable: `SIGNPATH_SIGNING_POLICY_SLUG`
- Variable: `SIGNPATH_PLUGIN_ARTIFACT_CONFIGURATION_SLUG`
- Variable: `SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION_SLUG`

Upload `signing/signpath-plugin.xml` and `signing/signpath-installer.xml` as the two SignPath artifact configurations. The workflow signs the plugin DLL first, verifies it, embeds it into the installer, then signs and verifies the installer. Signing runs only for a `v<version>` tag and requires manual approval in SignPath. The tag must match the version in `buildspec.json` and `CMakeLists.txt`.

The optional `DSK_OAUTH_APP_CONFIG_JSON` repository secret supplies the publisher-managed YouTube OAuth desktop application to release builds. Pull-request builds do not require it and produce a compatible build without bundled publisher credentials.

After signing both the built DLL and staged package DLL, enforce the local release gate with:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\validate-package.ps1 -BuildDir build\windows-x64-sdk3 -ObsPluginRoot path\to\staged\obs-dsk-multistream -ObsPluginScanRoot path\to\staged\obs-dsk-multistream -RequireValidSignature
```
