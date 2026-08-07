# DSK Account Connections

DSK keeps account login separate from output start/stop. Editing a target does not replace its saved connection until the edit is saved successfully.

## Twitch

`Login with Twitch` uses the DSK publisher OAuth relay at `https://auth.dasoku.org` with the dedicated `multistream` application profile. The desktop plugin opens the system browser, uses a loopback callback with PKCE, requests only `channel:read:stream_key`, and retrieves the authorized channel's stream key.

- `Connect` creates a connection for a target that has no saved account.
- The same button changes to `Disconnect` after login. Save the disconnected target, reopen it, and press `Connect` to choose another account.
- `Disconnect` clears the account/key in the editor. `Save` commits the removal; `Cancel` keeps the previous saved connection.
- Disconnecting in DSK does not revoke the DSK application's authorization on Twitch. Review or revoke connected applications in the Twitch account's Connections settings, then use DSK Disconnect or complete removal to delete the local key.
- The relay is needed only during login. RTMP streaming uses the stream key stored in Windows Credential Manager.

Legacy Twitch Client ID, Client Secret, and refresh-token fields are removed when publisher-managed Twitch targets are loaded or saved. They are not needed by distributed clients.

## YouTube

Distribution builds use the verified DSK publisher Google desktop OAuth application by default. A user selects `Login with YouTube` and presses `Connect`; no Google developer Client ID or Client Secret is entered on that machine. OAuth is direct between the desktop plugin and Google with PKCE and a loopback callback.

- The bundled DSK application has a published consent screen and completed Google's verification for its requested YouTube Live scope.
- Google treats installed applications as unable to keep a client secret confidential. The bundled desktop credential identifies the publisher app; user authorization remains protected by PKCE and the local callback.
- DSK stores the user's refresh token in Windows Credential Manager. Bundled publisher credentials are not copied into target settings or the settings JSON.
- `Use custom Google OAuth app` preserves the legacy path for advanced users and existing targets. Custom Client IDs and Client Secrets continue to be stored through the target's DSK credential references.
- Before RTMP starts, DSK lists ready YouTube broadcasts. One ready broadcast is selected automatically; multiple broadcasts require an explicit choice. DSK retrieves the selected broadcast's bound stream key and sends video only after that choice is confirmed. If no ready broadcast exists, DSK can create a new broadcast from the most recent compatible reusable completed broadcast.

Before DSK starts YouTube OAuth, the editor displays the DSK privacy policy,
YouTube Terms of Service, Google Privacy Policy, and Google permissions page.
Google's own OAuth consent screen requests the actual account authorization. Disconnecting YouTube
removes the saved account and refresh token after `Save`. The RTMP stream key
remains available for reconnecting. Cancelling the editor keeps the previous
saved connection. Access can also be revoked from
<https://security.google.com/settings/security/permissions>; revoking at Google
does not replace removal of the local credential.

## Kick

`Login with Kick` uses the DSK publisher OAuth relay and a dedicated `multistream` application profile. The desktop plugin opens the system browser, uses a loopback callback with PKCE, and requests `user:read`, `channel:read`, and `streamkey:read`.

The Kick developer application must enable all three matching permissions:
user information, channel information, and stream-key access. Kick can accept
the authorization request while omitting a permission that is disabled on the
developer application. In that case the relay rejects the incomplete token and
the plugin cannot retrieve the stream URL or key. After changing the developer
application permissions, reconnect Kick in the target editor to issue a new
grant.

Comment Viewer and Multistream use different Twitch and Kick provider applications. Multistream must never fall back to the Comment Viewer client registration, and a connection created before this separation may require one reconnect.

After authorization, DSK reads the authenticated channel from Kick's official `GET /public/v1/channels` endpoint and stores the returned RTMP(S) URL and stream key locally. The OAuth access and refresh tokens are not retained by the OBS target. The Comment Viewer keeps its separate Kick profile and does not receive the stream-key permission.

Existing manual Kick targets remain supported. Reconnecting replaces the saved URL and key only after the new login completes successfully and the target is saved.

Kick Partner Program members must enable Kick's Multistreaming toggle when
simulcasting and should review the current Partner Program payout conditions.
See [simulcast guidelines](simulcast-guidelines.md).

## Secret Storage

The settings JSON stores only DSK credential references. User stream keys, refresh tokens, and custom OAuth secrets are written to the current Windows user's Credential Manager under the `DSK Multistream/` namespace. Publisher OAuth values generated into a distribution build are not written into individual target settings.

Run the read-only first-run diagnostic without opening or modifying OBS:

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\diagnose-first-run.ps1 -OutputPath build\dsk-first-run-diagnostic.json
```

The report includes file hashes, signatures, duplicate plugin locations, the signed OBS libcurl runtime, settings parse results, credential-reference counts, recent DSK Code Integrity events, and publisher-relay readiness. It does not include stream keys, OAuth tokens, Client Secrets, or credential values.

Normal uninstall preserves these settings and credentials. The uninstaller's
explicit complete-removal option, or the installed
`tools\remove-user-data.ps1` helper, removes only the current Windows user's DSK
Multistream settings and `DSK Multistream/` credentials.
