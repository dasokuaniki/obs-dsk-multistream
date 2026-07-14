# DSK Runtime State Specification

## Goal

DSK must not use one generic `Live` label for every successful start request.
The user-facing state is split into two layers:

- Transport state: whether the OBS output/RTMP transport is starting, sending, reconnecting, stopping, or failed.
- Platform live state: whether the destination platform is actually live, still preparing, waiting for a stream signal, blocked by API/auth, or not API-verifiable.

This keeps YouTube honest: RTMP can be connected while the YouTube Live Broadcast is still not live.

## Runtime Model

`OutputTarget` remains the saved configuration record for compatibility.
Transient state is tracked by `TargetRuntimeStatus` inside `OutputManager`.
`OutputTarget.state` and `OutputTarget.lastError` are legacy/UI-compatibility
fields only. They may be mirrored while running, but new operational decisions
must read `TargetRuntimeStatus` first.

Runtime messages are also split:

- `transportMessage`: OBS/RTMP output lifecycle text such as `Connecting`, `RTMP sending`, `Stopping`.
- `platformMessage`: platform-specific text such as `YouTube broadcast is live`, `Reconnect YouTube login`.

For YouTube rows, `platformMessage` wins over a generic `RTMP sending` detail once transport is running.

Transport states:

- `Idle`: no runtime output.
- `Starting`: OBS accepted a start request or emitted `starting`.
- `Connected`: OBS emitted `start`.
- `Active`: OBS emitted `activate`; encoded data should be moving.
- `Reconnecting`: OBS emitted `reconnect`.
- `Stopping`: OBS emitted `stopping` or DSK requested stop.
- `Failed`: start/stop failed and the target is no longer running.

Platform live states:

- `NotApplicable`: the platform is not API-verifiable by DSK.
- `Unknown`: DSK has not checked platform state yet.
- `RtmpSignalOnly`: RTMP is connected, but platform Live state is not confirmed.
- `WaitingForSignal`: YouTube API is waiting for stream status to become active.
- `Preparing`: YouTube has a broadcast, but it is not ready for public live yet.
- `Testing`: YouTube monitor stream is testing.
- `LiveStarting`: YouTube is transitioning to live.
- `Live`: the platform confirmed public live state.
- `NeedsManualStart`: RTMP is connected, but the user must start or configure the broadcast manually.
- `AuthExpired`: RTMP may continue, but OAuth must be reconnected for automatic platform start.
- `QuotaBlocked`: RTMP may continue, but API quota prevents automatic platform start.
- `BroadcastMismatch`: RTMP is connected to a stream key that did not match the active broadcast.
- `MultipleBroadcasts`: DSK found multiple possible broadcasts and cannot safely choose one.
- `Failed`: platform API start failed for another reason.

## UI Rules

Stream Controls Dock rows use runtime state first:

- `Starting` / `Connecting`: show a disabled button.
- `RTMP sending`: show Stop, not Start.
- `YouTube preparing`: show Stop and a non-fatal detail line.
- `YouTube Live`: show Stop and a green/live state.
- `YouTube API warning`: keep the Stop button if RTMP is still sending.
- `Failed`: show Start and the short failure reason.

Main Dock state column uses the same runtime-derived label and tooltip.

## Race Safety

Every asynchronous YouTube API continuation must carry the current session serial.
If the target has been stopped and restarted, stale replies and timers are ignored.

Required guard:

```text
target id + session serial must still match the active session
```

## Source Of Truth

Start/stop decisions:

- Check `TargetRuntimeStatus` before `OutputTarget.state`.
- A target with `TransportState::Starting`, `Connected`, `Active`, or `Reconnecting` is already running.
- A target with `TransportState::Stopping` must not be started again until the session is released.
- `OutputTarget.state` may be updated for old UI paths, but must not be the only guard.

UI display:

- Use `runtimeStatusLabel()` and `runtimeStatusDetail()` when a runtime session exists.
- Fall back to `OutputTarget` validation only for idle targets.

## Next Verification

- Start one Twitch target and confirm only that row moves to Stop/RTMP sending.
- Start one YouTube target and confirm RTMP and YouTube platform states are distinct.
- Stop and quickly restart a YouTube target; stale API replies must not overwrite the new session.
- Force a YouTube auth/quota failure while RTMP is running; UI must show a warning, not a fatal output failure.
