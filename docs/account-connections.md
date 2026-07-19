# DSK Account Connections

DSK keeps account login separate from output start/stop. Editing a target does not replace its saved connection until the edit is saved successfully.

## Twitch

`Login with Twitch` uses the DSK publisher OAuth relay. The desktop plugin opens the system browser, uses a loopback callback with PKCE, requests only `channel:read:stream_key`, and retrieves the authorized channel's stream key.

- `Connect Twitch` creates a connection for a target that has no saved account.
- `Reconnect Twitch` repeats login and replaces the saved account/key only after the new login succeeds and the target is saved.
- `Disconnect` clears the account/key in the editor. `Save` commits the removal; `Cancel` keeps the previous saved connection.
- Disconnecting in DSK does not revoke the DSK application's authorization on Twitch. Revocation remains an account-side action on Twitch.
- The relay is needed only during login. RTMP streaming uses the stream key stored in Windows Credential Manager.

Legacy Twitch Client ID, Client Secret, and refresh-token fields are removed when publisher-managed Twitch targets are loaded or saved. They are not needed by distributed clients.

## YouTube

Distribution builds use the DSK publisher's bundled Google desktop OAuth application by default. A user selects `Login with YouTube` and presses `Connect YouTube`; no Google developer Client ID or Client Secret is entered on that machine. OAuth is direct between the desktop plugin and Google with PKCE and a loopback callback.

- The publisher must configure and build with a Google desktop OAuth application, publish the consent screen, and complete verification where Google requires it. Apps left in testing mode only accept configured test users.
- Google treats installed applications as unable to keep a client secret confidential. The bundled desktop credential identifies the publisher app; user authorization remains protected by PKCE and the local callback.
- DSK stores the user's refresh token in Windows Credential Manager. Bundled publisher credentials are not copied into target settings or the settings JSON.
- `Use custom Google OAuth app` preserves the legacy path for advanced users and existing targets. Custom Client IDs and Client Secrets continue to be stored through the target's DSK credential references.
- The target still needs its YouTube stream key. After RTMP input is active, DSK uses the authorized YouTube API connection to find the stream-key-matched broadcast and request its live transition.

Disconnecting YouTube removes the saved account and refresh token after `Save`. The RTMP stream key remains available for reconnecting. Cancelling the editor keeps the previous saved connection.

## Kick

`Login with Kick` uses the DSK publisher OAuth relay and a dedicated `multistream` application profile. The desktop plugin opens the system browser, uses a loopback callback with PKCE, and requests `user:read`, `channel:read`, and `streamkey:read`.

After authorization, DSK reads the authenticated channel from Kick's official `GET /public/v1/channels` endpoint and stores the returned RTMP(S) URL and stream key locally. The OAuth access and refresh tokens are not retained by the OBS target. The Comment Viewer keeps its separate Kick profile and does not receive the stream-key permission.

Existing manual Kick targets remain supported. Reconnecting replaces the saved URL and key only after the new login completes successfully and the target is saved.

## Secret Storage

The settings JSON stores only DSK credential references. User stream keys, refresh tokens, and custom OAuth secrets are written to the current Windows user's Credential Manager under the `DSK Multistream/` namespace. Publisher OAuth values generated into a distribution build are not written into individual target settings.

Run the read-only first-run diagnostic without opening or modifying OBS:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\diagnose-first-run.ps1 -OutputPath build\dsk-first-run-diagnostic.json
```

The report includes file hashes, signatures, duplicate plugin locations, the signed OBS libcurl runtime, settings parse results, credential-reference counts, recent DSK Code Integrity events, and publisher-relay readiness. It does not include stream keys, OAuth tokens, Client Secrets, or credential values.
