# DSK Multistream

DSK Multistream is an OBS Studio plugin for sending one OBS session to Twitch, YouTube, Kick, and Manual RTMP destinations.

This repository targets the OBS 32.x plugin stack on Windows x64. It provides native OBS docks, profile-based target settings, and independent horizontal and vertical canvas editors. The working tree contains the 0.5.1 Beta candidate; publication is subject to the release checks below.

## Scope

- Start OBS plus all selected targets / stop all outputs.
- Start / stop each target independently.
- OBS Main, DSK Horizontal (1920×1080), and DSK Vertical (1080×1920) are distinct output choices.
- Shared or dedicated encoders within each canvas group. Video encoders are not shared across different canvases; audio uses the existing OBS mix.
- Horizontal and vertical canvases with mouse editing, snapping, source ordering, and independent saved layouts and OBS scene links.
- One-click Twitch and Kick publisher login with automatic stream-key retrieval, publisher-configured YouTube OAuth, and manual RTMP targets.
- Manual RTMP also supports domestic services such as TwitCasting and OPENREC when the service has issued an RTMP URL and stream key for the account.
- Stream keys and OAuth secrets stored in Windows Credential Manager; JSON settings contain references only.
- Automatic integration with a separately installed DSK Comment Viewer. The browser dock appears only after the Viewer's loopback v1 API passes validation, and a YouTube RTMP start can request one loopback broadcast discovery without enabling background polling.

## Current Implementation Status

- OBS main streaming remains controlled by OBS; the Stream Controls dock mirrors the detected service icon and invokes OBS' normal start/stop action.
- `DSK Horizontal`: independent output with selectable shared or dedicated hardware/software encoders.
- `DSK Vertical`: 1080x1920 scene/canvas output with its own encoder group; its existing persistent OBS canvas is retained.
- Individual target starts work even when the target is excluded from `Start All`.
- Scene Routing remains an experimental implementation and is hidden from the beta UI by default. Internal testing can enable it before starting OBS with `DSK_EXPERIMENTAL_SCENE_ROUTING=1`.

## Separate scenes for different destinations

Open **Docks > DSK Horizontal** or **Docks > DSK Vertical**, then create a scene and add existing OBS sources with the `+` controls. In a destination's Edit dialog, select the matching DSK canvas and a DSK video encoder. Selecting **OBS Main** instead sends the normal OBS program composition.

The canvas preview shows the composition before encoding, not confirmation that a remote platform received it. OBS Link is off initially; enable it only when the DSK scene should follow an explicitly linked OBS scene. Horizontal and vertical Setup visibility and links are independent.

Version 0.5.0 writes settings schema 2 and reads schema 1 or 2. Before updating, preserve a separate copy of each OBS profile's `dsk-multistream.json`. To return to 0.4.1, close OBS and restore that pre-update copy as well as the old plugin. Do not change only the schema number or canvas identifier. See the [horizontal output test plan](docs/horizontal-output-test-plan.md).

## Twitch Login

Select `Login with Twitch` and press `Connect`. DSK opens Twitch in the system browser, requests only stream-key read access, retrieves the authorized channel's stream key, and stores it in Windows Credential Manager. Users do not provide a Twitch Client ID, Client Secret, or stream key.

The DSK publisher relay is used only while signing in. Once a target is saved, normal RTMP streaming uses the locally stored key and does not depend on the relay being online.

The same button changes to `Disconnect` after login. Save the disconnected target, reopen it, and press `Connect` to choose another account. Cancelling the editor keeps the previously saved connection. See `docs/account-connections.md` for the Twitch, YouTube, and Kick connection boundaries.

## YouTube Login

Distribution builds bundle the verified DSK Google desktop OAuth application. Users select `Login with YouTube` and press `Connect` without entering a Client ID or Client Secret. Before RTMP begins, DSK lists ready YouTube broadcasts, asks the user to choose when more than one exists, retrieves the selected broadcast's bound stream key, and starts only that target. If no ready broadcast exists, DSK can create one from the most recent reusable completed broadcast. Existing targets that use a custom Google OAuth app remain supported through `Use custom Google OAuth app`.

For a read-only first-run and distribution diagnostic that does not launch or modify OBS:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\diagnose-first-run.ps1 -OutputPath build\dsk-first-run-diagnostic.json
```

## Build

Install OBS Studio build dependencies and Qt 6, then run the preflight check:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\check-prereqs.ps1 -ObsPrefix deps\obs-sdk -ObsDepsPrefix deps\obs-studio-32.1.2\.deps\obs-deps-2025-08-23-x64 -QtPrefix deps\obs-studio-32.1.2\.deps\obs-deps-qt6-2025-08-23-x64
```

Configure with CMake:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\configure-windows.ps1 -ObsPrefix deps\obs-sdk -ObsDepsPrefix deps\obs-studio-32.1.2\.deps\obs-deps-2025-08-23-x64 -QtPrefix deps\obs-studio-32.1.2\.deps\obs-deps-qt6-2025-08-23-x64 -OAuthAppConfig C:\private\oauth-app-config.json
cmake --build build\windows-x64-sdk3 --parallel
ctest --test-dir build\windows-x64-sdk3 --output-on-failure
```

See `docs/windows-build-and-install.md` for full Windows install/testing steps.

Comment Viewer is an independent product and is never bundled or removed by this plugin. See `docs/comment-viewer-integration.md` for the versioned localhost contract and failure behavior.

## Windows Installer

After building the plugin, create the distributable installer with:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\build-windows-installer.ps1 -BuildDir build\windows-x64-sdk3
```

The generated `release\DSK-Multistream-<version>-Windows-x64-Setup.exe` installs the plugin for all users and registers it in Windows **Settings > Apps > Installed apps**. Running a newer installer updates the same installation. Normal uninstall removes only the dedicated plugin binaries and static data while preserving settings and credentials for reinstall. The uninstaller also offers an explicit complete-removal choice that deletes only DSK Multistream-owned settings and credentials. Limited-beta builds include `docs/beta-distribution.md` with installation and security guidance.

## Privacy

DSK Multistream has no analytics or advertising. Network requests occur only for features the user configures or invokes, including OAuth account connection, streaming to configured RTMP destinations, YouTube Live control, and the local DSK Comment Viewer integration. See the [privacy policy](docs/privacy.md) for the complete list.

## Code signing policy

Release builds are produced from reviewed source and must have valid Authenticode
signatures and trusted timestamps on the plugin DLL, generated uninstaller, and
Windows installer before public download. The unsigned development candidate is
not the public signed artifact. See the [code signing policy](docs/code-signing-policy.md).

## Support the project

DSK Multistream is free and open-source software. Sponsorships and donations support maintenance, release signing, and compatibility testing. The plugin and its security fixes remain available to everyone under the same GPL license.

## License

The DSK Multistream source code is GPL-2.0-or-later. Third-party trademarks and
brand assets identified in `docs/third-party-notices.md`, including the official
Kick icon, remain the property of their respective owners and are not licensed
under the GPL.
