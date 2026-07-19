# Privacy Policy

Last updated: 2026-07-20

DSK Multistream does not include analytics, advertising, crash-report uploads, or background telemetry.

The plugin transfers information to networked systems only when the user configures or invokes a feature that requires it:

- Twitch account connection uses Twitch OAuth and API endpoints. Publisher-managed login temporarily uses the DSK OAuth relay to complete the authorization exchange.
- Kick account connection uses Kick OAuth and API endpoints. Publisher-managed login temporarily uses the DSK OAuth relay to complete the authorization exchange.
- YouTube account connection and live-broadcast control use Google OAuth and YouTube Data API endpoints.
- Streaming sends audio, video, and the configured stream key to each RTMP or RTMPS destination selected by the user.
- Help links open the relevant platform's website in the user's browser.
- The optional DSK Comment Viewer integration communicates only with `127.0.0.1:17321` on the local computer.

OAuth refresh tokens, stream keys, and OAuth client secrets are stored in Windows Credential Manager. DSK configuration files store credential references rather than the secret values. Removing the plugin does not automatically delete user profiles or stored credentials, so reinstalling does not erase streaming configuration. The repository includes a maintenance script for deleting orphaned DSK credentials when the user explicitly chooses to do so.

DSK does not sell personal data. The external services used by a configured connection process information under their own privacy policies. Users should review the policies of Twitch, Google/YouTube, Kick, TikTok, and any custom streaming provider they configure.

Security issues can be reported privately through the GitHub repository's security advisory feature.
