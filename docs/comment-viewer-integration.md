# DSK Comment Viewer integration

DSK Multistream and DSK Comment Viewer are installed, updated, licensed, and removed independently. Multistream is usable without Comment Viewer and does not bundle or manage Viewer files, settings, accounts, or credentials.

At OBS startup, the integration is automatically on when a complete Viewer installation is present in `%LOCALAPPDATA%\DSKCommentViewer`; otherwise it starts off. The **Comment Viewer integration** check in the `DSK Streaming` gear menu controls the integration for the current OBS session. When enabled, Multistream first requests:

```http
GET http://127.0.0.1:17321/api/integrations/obs/v1
Accept: application/json
```

If the first request fails, Multistream starts the independent Viewer server and retries the same endpoint for up to roughly 15 seconds. An already-running Viewer is not started again. The validated v1 response identifies `dsk-comment-viewer`, declares the `obs-browser-dock` integration, and exposes the fixed path `/viewer?dock=chat&send=1`. Multistream constructs and accepts only `http://127.0.0.1:17321/viewer?dock=chat&send=1`; it never follows a dock host or arbitrary URL supplied by the response.

The integration endpoint contains no tokens, account details, comment content, or stream keys. Comment sending remains inside the Viewer and is protected by Viewer's loopback and request-origin checks.

After validation, Multistream creates the plugin-owned `DSK Comments` browser dock with ID `dskcommentsviewer`. If Viewer is absent, incompatible, or unhealthy, no dead dock is created and all streaming features remain available. The optional **Open DSK Comment Viewer** Tools item appears when the independent Viewer installation is detected. Opening Viewer from that item also starts a fresh integration probe, so the dock can recover after an earlier startup timeout without restarting OBS.

## Known regression guard: intermittent white Dock

Do not make the API validation URL and the OBS Browser navigation URL use the same hostname.

- Keep validating the Viewer contract against exactly `http://127.0.0.1:17321/viewer?dock=chat&send=1`.
- Navigate the `DSK Comments` OBS Browser widget to the equivalent `http://localhost:17321/viewer?dock=chat&send=1`.
- Do not replace the non-blocking browser initialization retry with `wait_for_browser_init()` on the OBS UI thread.
- Do not add timed browser recreation or repeated reloads as a workaround.

Comment Viewer and existing browser sources can hold six long-lived connections to `127.0.0.1:17321`. That exhausts the OBS Chromium HTTP/1.1 per-host connection pool, so a Dock created later can remain on a white page depending on startup order. Using `localhost` only for the Dock keeps the same loopback server and API contract while giving the Dock a separate Chromium host pool.

This regressed intermittently before 2026-07-30 and can appear fixed after a restart merely because the startup order changed. A successful probe or HTTP 200 alone is therefore insufficient. Every change touching this path must pass `dsk-comment-viewer-dock-rendering-test` and the full CTest suite, then be verified with two OBS starts. Both starts must log a navigation to the `localhost` URL and a title change to `DSK Comment Viewer`; at least one must be visually checked for rendered controls rather than a white surface.

Turning **Comment Viewer integration** off cancels pending checks and removes only the `dskcommentsviewer` dock for the current OBS session. It does not stop Viewer or remove Viewer files, settings, accounts, or credentials. If Viewer remains installed, the integration starts on again at the next OBS startup.

After YouTube confirms that the selected broadcast is actually `live`, Multistream sends one loopback-only `POST` notification to:

`http://127.0.0.1:17321/api/integrations/obs/v2/youtube-live-start`

```json
{
  "sessionId": "youtube:<one-time-id>",
  "broadcastId": "<selected 11-character YouTube video ID>"
}
```

Viewer uses `broadcastId` as a one-time preferred target for that discovery and does not replace its saved YouTube setting. Each successful 11 h 30 min archive switch sends the new live broadcast ID. The signal does not enable background discovery. Viewer versions that do not expose the v2 endpoint receive the legacy `/api/youtube/recheck` request instead, so updating the two products in either order remains safe. Updated Viewer builds also accept an older v2 notification without `broadcastId` and fall back to their saved discovery target.

The full cross-project contract and compatibility rules are maintained in the Viewer repository as `DSK_MULTISTREAM_INTEGRATION_HANDOFF.md`. Breaking changes require a versioned v2 endpoint instead of silently changing v1.
