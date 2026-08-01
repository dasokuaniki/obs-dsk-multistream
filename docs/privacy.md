# DSK Multistream Privacy Policy

Last updated: 2026-07-31

DSK Multistream is a free OBS Studio plugin. It does not include advertising,
behavioral analytics, background telemetry, or automatic crash-report uploads.

## Data used by the plugin

The plugin processes data only for features the user configures or invokes:

- RTMP/RTMPS streaming sends the selected OBS audio and video and the configured
  stream key directly from the user's computer to each selected destination.
  DSK does not operate an audio/video relay for Multistream.
- Twitch login requests `channel:read:stream_key`. It reads the authorized
  account identity and stream key so the user can configure that destination.
- Kick login requests `user:read`, `channel:read`, and `streamkey:read`. It reads
  the authorized account identity, RTMP(S) URL, and stream key.
- YouTube login requests `https://www.googleapis.com/auth/youtube.force-ssl`.
  DSK lists the user's reusable live streams and upcoming/active live broadcasts,
  reads their status and binding, binds or transitions the broadcast selected by
  the user, and, when the optional 11 h 30 min archive rotation is enabled,
  creates and starts the next broadcast using the current broadcast settings.
- Optional DSK Comment Viewer integration communicates only with
  `127.0.0.1:17321` on the same computer. The integration does not transfer chat
  messages, OAuth tokens, or stream keys.
- Help, policy, and account-management actions open the relevant website in the
  user's browser.

Manual RTMP destinations are chosen and configured by the user. Their operators
process the transmitted stream and key under their own terms and privacy policy.

## OAuth relay and external services

YouTube OAuth and YouTube Data API requests go directly between the plugin and
Google. Twitch and Kick publisher-managed login temporarily uses
`https://auth.dasoku.org` so provider client secrets do not have to be embedded in
the public plugin.

The DSK OAuth relay keeps pending authorization state, authorization codes, and
token-exchange results in memory only. Entries expire after at most 10 minutes
and are removed after a completed, non-retryable exchange. The relay does not use
a database for these items. Its application log records only the HTTP method and
URL path; it excludes query strings, authorization codes, tokens, and stream
keys. Provider access or refresh tokens may pass through relay memory during the
exchange, then the desktop plugin uses the returned access token for the
requested account lookup.

External service policies include:

- [YouTube Terms of Service](https://www.youtube.com/t/terms)
- [Google Privacy Policy](https://policies.google.com/privacy)
- [Twitch Privacy Notice](https://www.twitch.tv/p/legal/privacy-notice/)
- [Kick Privacy Policy](https://kick.com/privacy-policy)

## Storage and retention

DSK destination, layout, and output settings are stored in each OBS profile as
`dsk-multistream.json`, with safety backup or corruption-recovery files when
needed. The plugin's fallback module settings are stored under the OBS
`obs-dsk-multistream` configuration directory.

Stream keys, Google refresh tokens, and custom OAuth client secrets are stored in
Windows Credential Manager under the current user's `DSK Multistream/`
namespace. Settings files contain credential references instead of those secret
values. Twitch and Kick access/refresh tokens are not retained by the saved OBS
target after the stream configuration has been retrieved. DSK does not sell or
share these local values for advertising.

OBS and DSK diagnostic logs may remain on the user's computer according to OBS
log retention. DSK redacts known token, client-secret, stream-key, and
authorization-code fields from provider errors. Users should still review a log
before sending it in a support request.

## Access removal and data deletion

- In a destination editor, `Disconnect` followed by `Save` removes that target's
  saved DSK account authorization or stream key. `Cancel` leaves it unchanged.
- Google access can also be reviewed or revoked in
  [Google Account permissions](https://security.google.com/settings/security/permissions).
  Revoking at Google prevents further token refresh; use DSK Disconnect or the
  complete-removal tool to delete the local credential too.
- Twitch and Kick grants can be revoked in the connected-app settings provided
  by those services. Revoking a provider grant does not by itself delete the
  locally stored stream key.
- Normal uninstall removes the plugin and keeps settings and credentials so an
  upgrade or reinstall does not erase the user's configuration.
- Complete removal is available from the uninstaller and from the installed
  `tools/remove-user-data.ps1` helper. It deletes only DSK Multistream settings,
  its module configuration directory, and current-user Credential Manager items
  beginning with `DSK Multistream/`. It does not delete OBS profiles or another
  DSK product's data.

For deletion, privacy, or security questions, contact `support@dasoku.org`. For
security vulnerabilities, the GitHub repository's private security advisory
feature is preferred.

Material changes to the purpose, scope, or storage of data will update this date
and, where appropriate, require another in-product confirmation.
