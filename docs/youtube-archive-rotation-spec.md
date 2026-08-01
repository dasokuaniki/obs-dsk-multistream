# YouTube archive rotation mode

## Goal

Add an opt-in YouTube delivery mode that keeps one RTMP upload running while DSK moves it to a new YouTube broadcast before a 12-hour archive boundary.

## User-visible behavior

- YouTube targets offer two delivery modes: `Normal` and `Split archives every 11 h 30 min`.
- `Normal` remains the default for existing and newly created targets.
- Archive rotation is available only with `Login with YouTube`; manual RTMP cannot call the required YouTube APIs.
- At 11 h 30 min from the YouTube broadcast's actual start time, DSK creates the next broadcast, binds it to the same YouTube stream, completes the current broadcast, and transitions the next broadcast to live.
- OBS output and the RTMP connection remain running during the frame change.
- After YouTube confirms the new frame is live, DSK asks a running Comment Viewer on its fixed loopback API to recheck the active YouTube chat.
- The next title uses the current title followed by `(Part N)` and copies the description, privacy setting, audience setting, DVR/recording setting, and latency preference where available.
- The mode repeats for each subsequent 11 h 30 min interval and recovers its schedule from YouTube's actual start time after a plugin or OBS restart.

## Safety and failure behavior

- DSK never completes the current broadcast until the next broadcast has been created and bound successfully.
- An error before that point leaves the current broadcast live and reports a recoverable warning.
- DSK waits for YouTube to confirm that the current broadcast is complete before transitioning the next broadcast to live.
- If YouTube temporarily omits the new broadcast's actual start time, DSK uses the confirmed switch time rather than the original RTMP session start, preventing an immediate second split.
- Once the current archive is confirmed complete, a non-recoverable next-frame start failure enters a bounded attention state instead of repeating indefinitely.
- DSK does not stop the Viewer or OBS output and does not depend on Live Redirect.
- OAuth tokens and stream keys are never written to logs.

## Compatibility

- Missing or unknown saved mode values decode as `Normal`.
- The setting is ignored/rejected when the target is not a YouTube OAuth target.
- Existing targets retain their current single-broadcast behavior.

## Acceptance criteria

1. Mode selection round-trips through settings and the target editor.
2. Normal mode behavior is unchanged.
3. Rotation time and next-title logic have deterministic unit tests.
4. API errors before current-frame completion cannot end the current broadcast.
5. A successful cycle creates, binds, completes, starts, confirms, and schedules the next cycle without restarting RTMP.
6. Focused tests, the full test suite, and a plugin build pass.

## Virtual verification

The no-OBS `dsk-youtube-archive-rotation-simulation-tests` target advances a deterministic clock across five consecutive archive splits. It verifies the exact 11 h 30 min boundary, generated broadcast metadata, restart recovery, missing `actualStartTime` fallback, and bounded handling for failures before and after the current archive completes.
