# Twitch Dual Format support specification

Status: approved for implementation on 2026-08-30

## Goal

Use the existing `DSK Vertical` layout as Twitch's vertical Additional Canvas
without changing the user's normal DSK Streaming controls.

The existing OBS-native Twitch row remains the Twitch start/stop control. `Start
All` starts that OBS-native stream plus the enabled non-Twitch DSK targets.

## Authoritative integration path

- OBS Studio 32.0 or newer.
- Twitch is configured as OBS's native streaming service.
- Enhanced Broadcasting is enabled in OBS Stream settings.
- `DSK Vertical` is selected as the Additional Canvas.
- One OBS native start operation sends the horizontal and vertical Twitch
  variants. DSK must not open a second independent Twitch RTMP connection.

References:

- https://help.twitch.tv/s/article/dual-format-vertical-video
- https://blog.twitch.tv/en/2026/06/17/introducing-dual-format-and-2k-streaming-on-twitch/
- https://docs.obsproject.com/reference-canvases

## Required behavior

1. `DSK Vertical` is a frontend-owned, non-ephemeral OBS canvas and is visible
   in OBS's Additional Canvas selector.
2. Per-target DSK scene canvases and preview canvases stay ephemeral and must
   not appear in that selector.
3. The vertical canvas is prepared after OBS finishes loading and after profile
   or scene-collection changes, so the selector and native stream can resolve a
   stable canvas UUID before streaming starts.
4. A scene restored by OBS after an abnormal shutdown is adopted and rebuilt;
   the plugin must not create duplicate `DSK Vertical Program` scenes.
5. Dual Format is active only when all of the following are true:
   - OBS native service is Twitch;
   - Enhanced Broadcasting is enabled;
   - OBS's selected Additional Canvas UUID matches `DSK Vertical`.
6. While Dual Format is active, independent DSK Twitch targets are excluded
   from Start All, OBS-linked auto-start, and individual start. They remain
   editable and are not deleted.
7. The OBS-native Twitch row and its start/stop button remain unchanged. Its
   detail text reports whether Enhanced Broadcasting or the Additional Canvas
   still needs configuration, or whether Dual Format is ready.
8. YouTube, Kick, TikTok/private targets, DSK Vertical independent outputs, and
   Stop All behavior remain compatible.
9. The plugin does not silently enable Enhanced Broadcasting or rewrite the
   user's OBS service settings.

## Safety and lifecycle

- Do not replace or resize a selected `DSK Vertical` canvas while the OBS native
  stream is active.
- Keep the existing vertical layout dimensions; this feature does not add a
  second resolution editor.
- Keep audio on OBS's main audio mixer. The DSK canvas is not given the
  `MIX_AUDIO` flag, avoiding an extra audio mix from duplicated scene sources.
- The OBS Canvas API is explicitly unstable, so the implementation is covered
  by a source contract test and an OBS 32.1.2 Windows canary before release.

## Acceptance criteria

- `DSK Vertical` appears once in OBS Stream > Additional Canvas.
- Selecting it with Enhanced Broadcasting produces a ready state.
- The existing OBS-native Twitch button starts/stops the native stream.
- Start All starts OBS Twitch once and starts other enabled DSK targets.
- No independent DSK Twitch RTMP output is created in Dual Format mode.
- Scene collection/profile changes preserve recovery without duplicate canvases
  or scenes.
- Unit, UI/static contract, full CTest, and Windows plugin build pass.
- Manual Twitch canary verifies horizontal and vertical playback before a
  signed distribution build is created.
