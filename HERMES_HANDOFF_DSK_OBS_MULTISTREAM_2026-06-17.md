# HERMES handoff: DSK OBS Multistream

Date: 2026-06-17
Workspace: C:\Users\dasok\Documents\Codex\2026-06-02\obs-twitch-youtube-kick-tiktok
Related viewer repo: C:\Users\dasok\Documents\Codex\2026-05-13\new-chat

## Role split

- Hermes: manager/editor. Keep task scope clear, preserve context, prepare briefs, track decisions.
- Codex: implementation, local investigation, builds, tests, installs when OBS is closed.
- Claude: review Codex output for design risk, crash risk, and missing verification.
- Human: final approval for live-stream impact, OAuth/API credentials, external service setup, irreversible operations.

There is no direct Hermes messaging bridge in this Codex toolset. Use this file as the authoritative handoff packet. If Hermes needs execution, create a clear queue task for Codex/Claude rather than vague chat instructions.

## Product goal

DSK is an OBS 32.x plugin for multistreaming to Twitch, YouTube, Kick, TikTok, etc.

Core product direction:

- Multiple stream targets.
- Individual Start/Stop per service.
- Start All / Stop All controls.
- Horizontal output based on normal OBS canvas.
- Vertical output with separate DSK vertical layout and scene linking.
- DSK Comment Viewer integration through an OBS browser dock.
- YouTube and Twitch login flows where useful.
- Keep simple/lightweight UX where possible; do not expose debug details in normal docks.

## Current important state

- OBS version in use: OBS 32.1.2.
- OBS profile shown in window title: "Untitled" / Japanese OBS profile name.
- OBS is currently running as `obs64`; do not overwrite plugin DLL while OBS is running.
- This directory is not a git repository.
- Installed DLL:
  - `C:\ProgramData\obs-studio\plugins\obs-dsk-multistream\bin\64bit\obs-dsk-multistream.dll`
  - timestamp observed: `2026/06/17 12:51:00`
- Current build output exists at:
  - `build\windows-x64-sdk3\obs-dsk-multistream.dll`
- The newest source edits after the 12:51 install have been built and smoke-tested, but not installed because OBS was running.

## Most recent issue

User reported:

> DSK Multistream dock shows an error.

Observed log root cause:

- YouTube RTMP output starts successfully using NVENC.
- RTMP connects to `rtmp://a.rtmp.youtube.com/live2`.
- YouTube API/OAuth refresh fails with `invalid_grant`, meaning the refresh token is expired or revoked.

Interpretation:

- This is not an encoder failure.
- This is not a stream-key/RTMP failure.
- This is a YouTube login/API auto-start warning.
- The user can still stream via RTMP, especially if YouTube Studio auto-start handles the broadcast.
- The user should reconnect YouTube login when automatic YouTube Live transition is desired.

## Latest source changes not yet installed

Files changed:

- `src/core/output-manager.cpp`
  - Added user-facing YouTube API warning text.
  - Keeps detailed API failure in OBS log.
  - Activity/status now says short actionable text, such as reconnect YouTube login, instead of dumping raw `invalid_grant`.
- `src/ui/main-dock.cpp`
  - Classifies YouTube API warnings separately from blocking errors.
  - Live YouTube warning remains `LIVE`, not red `Error`.
  - Stopped target with expired YouTube login shows `Needs login` / yellow warning, not red failure.
  - Route check treats YouTube API login warning as ready-with-warning instead of fatal issue.
- `src/ui/stream-controls-dock.cpp`
  - Previous edit changed service row detail from long raw failures to shorter labels such as `Live - YouTube login expired`.

## Verification already run

Build command:

```powershell
$cmd = '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build\windows-x64-sdk3 --config RelWithDebInfo --target obs-dsk-multistream dsk-ui-smoke-tests dsk-smoke-tests'
cmd.exe /c $cmd
```

Smoke test command:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build\windows-x64-sdk3 -C RelWithDebInfo -R "dsk-(ui-smoke|smoke)-tests" --output-on-failure
```

Result:

- `dsk-smoke-tests` passed.
- `dsk-ui-smoke-tests` passed.

## Install command

Only run this when OBS is fully closed:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install-user-plugin.ps1 -BuildDir build\windows-x64-sdk3
```

After install, verify:

```powershell
Get-Item "C:\ProgramData\obs-studio\plugins\obs-dsk-multistream\bin\64bit\obs-dsk-multistream.dll" | Select-Object FullName,Length,LastWriteTime
```

## Immediate next actions for Hermes

1. Ask Codex to install the current build after OBS is closed.
2. Start OBS and verify no startup crash.
3. Verify three main docks load:
   - `DSK Multistream`
   - `DSK Stream Controls`
   - `DSK Vertical Layout`
   - `DSK Comments` should be an OBS browser dock pointing to the standalone viewer.
4. Start YouTube target if safe to test.
5. Confirm UI behavior:
   - RTMP success + expired Google refresh token must not show red fatal error.
   - DSK Multistream row should show `LIVE` while live.
   - If stopped and login is expired, row should show `Needs login` / warning.
   - Activity should show a short actionable warning, not raw JSON/API failure.
6. If user wants automatic YouTube Live transition, reconnect YouTube login.

## Medium priority follow-ups

- Product-mode OAuth:
  - Current Google Client ID/Secret entry is acceptable for development, but not for public distribution.
  - Shipped product should use DSK-owned Google OAuth credentials, with user consent only.
  - Manual RTMP + YouTube Studio auto-start should remain as a fallback.
- Comment viewer:
  - OBS dock should use the browser dock for exact visual parity with standalone DSK Comment Viewer.
  - Avoid rebuilding chat UI in Qt unless there is a strong reason.
- YouTube comments:
  - Current desired direction is API-less public web chat parsing where possible.
  - Legacy API Key UI in DSK Comment Viewer can be hidden/removed later to avoid confusion.
- UI:
  - Keep operation docks simple.
  - Hide debug-only or constant non-actionable status.
  - Avoid showing raw API errors in normal streamer-facing UI.

## Safety constraints

- Do not install or replace the OBS plugin DLL while OBS is running.
- Do not stop OBS while the user may be streaming unless explicitly told.
- Do not delete scenes, sources, profiles, or existing OBS settings unless explicitly requested.
- Do not write secrets, OAuth tokens, stream keys, API keys, or refresh tokens into handoff files or logs.
- Do not broaden into a large refactor when the user reports a concrete crash or display issue.
- Preserve the standalone DSK Comment Viewer as its own app while integrating it with OBS.

## Useful commands

Check OBS process:

```powershell
Get-Process obs64 -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,StartTime,MainWindowTitle
```

Recent OBS logs:

```powershell
Get-ChildItem "$env:APPDATA\obs-studio\logs" -Filter *.txt | Sort-Object LastWriteTime -Descending | Select-Object -First 6 FullName,LastWriteTime,Length
```

Search current OBS log for DSK/YouTube:

```powershell
rg -n "DSK Multistream|YouTube|invalid_grant|YouTube token refresh|Started YouTube|Stopped YouTube|failed|error|warning" "$env:APPDATA\obs-studio\logs\LATEST_LOG_FILE.txt"
```

DSK settings file location:

```powershell
$path = Join-Path $env:APPDATA 'obs-studio\basic\profiles\無題\dsk-multistream.json'
```

Read sanitized target state:

```powershell
$json = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
$json.targets | Select-Object name,platformId,authMode,enabled,encoderGroup,state,lastError | Format-List
```

## Escalate to human

Escalate before:

- Closing OBS.
- Starting/stopping real live streams.
- Changing Google Cloud/OAuth app settings.
- Handling stream keys, OAuth secrets, refresh tokens, or API keys.
- Removing OBS scenes/sources.
- Publishing/releasing the plugin.

## Suggested Hermes brief template

Use this shape when passing work to Codex:

```text
repo_path=C:\Users\dasok\Documents\Codex\2026-06-02\obs-twitch-youtube-kick-tiktok

Task:
<specific request>

Scope:
<files/modules/docks involved>

Non-goals:
<what not to touch>

Constraints:
- Do not touch OBS while streaming.
- Do not expose secrets.
- Keep UI streamer-facing and concise.

Success criteria:
<observable user-facing result>

Verification:
<build/test/log/screenshot/manual check>
```

