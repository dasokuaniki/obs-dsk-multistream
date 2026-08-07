# DSK Comment Viewer integration

DSK Multistream and DSK Comment Viewer are installed, updated, licensed, and removed independently. Multistream is usable without Comment Viewer and does not bundle or manage Viewer files, settings, accounts, or credentials.

At OBS startup, Multistream looks first for the standard Viewer in `%LOCALAPPDATA%\DSKCommentViewer`. If it is absent, Multistream falls back to the Twitch-focused Viewer in `%LOCALAPPDATA%\DSKTwitchCommentViewer`. The standard Viewer remains preferred when both products are installed. Each installation must match its fixed runtime profile; arbitrary install folders, ports, and remote hosts are not accepted.

The standard Viewer uses:

```http
GET http://127.0.0.1:17321/api/integrations/obs/v1
Accept: application/json
```

The Twitch-focused Viewer uses the same contract on its independent fixed port:

```http
GET http://127.0.0.1:17325/api/integrations/obs/v1
Accept: application/json
```

If the first request fails, Multistream starts the detected independent Viewer server and retries the selected endpoint for up to roughly 15 seconds. An already-running Viewer is not started again. The validated v1 response identifies `dsk-comment-viewer`, declares the `obs-browser-dock` integration, and exposes the fixed path `/viewer?dock=chat&send=1`. Multistream constructs the URL from the detected fixed loopback endpoint only; it never follows a dock host or arbitrary URL supplied by the response.

The integration endpoint contains no tokens, account details, comment content, or stream keys. Comment sending remains inside the Viewer and is protected by Viewer's loopback and request-origin checks.

When Viewer is installed, Multistream registers the plugin-owned `DSK Comments` dock shell with ID `dskcommentsviewer` in the same startup phase as its other dock shells. After validation, the OBS Browser widget is attached inside that existing shell. Background discovery and reconnection must not register, remove, show, raise, or resize the dock. This keeps OBS's main-preview fit and scroll state consistent with the restored dock layout. If Viewer is incompatible or temporarily unhealthy, the stable empty shell remains and all streaming features remain available. The optional **Open DSK Comment Viewer** Tools item appears when the independent Viewer installation is detected. Opening Viewer from that item also starts a fresh integration probe, so the browser can recover after an earlier startup timeout without restarting OBS.

## Known regression guard: intermittent white Dock

Do not make the API validation URL and the OBS Browser navigation URL use the same hostname.

- Keep validating the Viewer contract against exactly the detected fixed endpoint: port `17321` for the standard Viewer or `17325` for the Twitch-focused Viewer.
- Navigate the `DSK Comments` OBS Browser widget to the equivalent `localhost` URL on the same selected port.
- Do not replace the non-blocking browser initialization retry with `wait_for_browser_init()` on the OBS UI thread.
- Do not add timed browser recreation or repeated reloads as a workaround.

Comment Viewer and existing browser sources can hold six long-lived connections to `127.0.0.1:17321`. That exhausts the OBS Chromium HTTP/1.1 per-host connection pool, so a Dock created later can remain on a white page depending on startup order. Using `localhost` only for the Dock keeps the same loopback server and API contract while giving the Dock a separate Chromium host pool.

This regressed intermittently before 2026-07-30 and can appear fixed after a restart merely because the startup order changed. A successful probe or HTTP 200 alone is therefore insufficient. Every change touching this path must pass `dsk-comment-viewer-dock-rendering-test` and the full CTest suite, then be verified with two OBS starts. Both starts must log a navigation to the `localhost` URL and a title change to `DSK Comment Viewer`; at least one must be visually checked for rendered controls rather than a white surface.

When Comment Viewer has just created and bound a YouTube broadcast, Multistream checks the following loopback-only endpoint before it sends RTMP video:

`GET http://127.0.0.1:17321/api/integrations/obs/v2/youtube-broadcast-selection`

The response contains only the prepared 11-character YouTube broadcast ID and its scheduled time. Multistream uses it only when the user has not already selected a broadcast in Multistream. It then performs the normal YouTube broadcast/stream-key lookup and the existing same-key auto-start conflict checks before sending video. A missing, old, invalid, or unavailable Viewer response falls back to the existing YouTube selection flow. No OAuth token, stream key, or account data is exchanged, and browser-origin requests are rejected.

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
