# Changelog

All notable user-facing changes are documented in this file.

## [0.4.0] - Development

### Added

- DSK Vertical can now be selected as OBS's Additional Canvas for Twitch Dual
  Format with Enhanced Broadcasting.

### Changed

- The existing OBS-native Twitch row remains the single Twitch start/stop
  control in Dual Format mode. Independent DSK Twitch starts are suppressed to
  prevent a duplicate Twitch RTMP connection, while an already-running legacy
  connection can still be stopped safely.

## [0.3.8] - Beta

### Fixed

- Windows release builds now Authenticode-sign and timestamp the generated
  uninstaller with the same publisher certificate as the plugin DLL and setup
  executable.
- Release and installer E2E checks now reject a missing, unsigned, differently
  signed, or untimestamped generated uninstaller.

### Changed

- Release signing supports Inno Setup's external two-pass signed-uninstaller
  workflow so the plugin DLL, generated uninstaller, and final setup can all be
  signed on the private SSL.com signing host without depending on Windows CKA.

## [0.3.7] - Beta

### Fixed

- YouTube now resolves the selected broadcast and its bound stream key before
  RTMP starts. A single ready broadcast is used automatically, while multiple
  broadcasts require an explicit choice.
- When no ready broadcast exists, YouTube can create a new broadcast from the
  most recent compatible reusable completed broadcast without sending video to
  an unconfirmed key.
- The DSK Comments dock shell is registered before background Viewer discovery,
  preventing reconnect timing from changing the OBS dock layout or preview fit.
- Standard and Twitch-focused Comment Viewer installations keep their fixed,
  isolated loopback ports throughout each probe.

### Changed

- Account setup uses one `Connect` / `Disconnect` button and keeps YouTube data
  use and policy links visible without a redundant local consent checkbox.
- The public beta guide now reflects the approved Google OAuth application,
  current signing gate, and complete-removal behavior.

## [0.3.6] - Beta

### Fixed

- YouTube now imports a broadcast just created by DSK Comment Viewer before
  RTMP transmission while preserving manual selection and same-key Auto-start
  conflict protection.
- YouTube no longer shows a false selected-broadcast warning while the API is
  still propagating the stream from ready to active immediately after RTMP connects.
- The Vertical dock toolbar now compacts at narrow widths instead of forcing the
  main OBS preview to shrink excessively.

## [0.3.5] - Beta

### Fixed

- YouTube no longer shows a false selected-broadcast warning while the API is
  still propagating the stream from ready to active immediately after RTMP connects.
  The confirmed selection is retried for a bounded 10-second window before the
  existing safe warning is shown.

### Changed

- The Kick destination badge now uses Kick's unmodified official Green Icon from
  the first-party KICK Brand Hub. The bundled notice identifies the icon as a
  Kick-owned trademark asset outside the project's GPL license.

## [0.3.4] - Beta

### Fixed

- Google and Twitch login now keep the local OAuth callback listener available
  for 15 minutes, allowing manual verification screens to complete safely.
- Package validation now follows the public four-preset policy and can verify
  a safely named development-slot DLL without mistaking it for the release DLL.

### Changed

- The neutral public Kick badge now uses the same frame, spacing, and centered
  monochrome typography as the other fallback badges, without a trademark image
  or brand-color imitation.

## [0.3.3] - Beta

### Fixed

- YouTube now confirms the selected broadcast and checks conflicting Auto-start
  broadcasts before RTMP transmission, preventing another broadcast that shares
  the reusable stream key from starting first.
- YouTube account connection cannot begin until the user reviews the data-use
  links and explicitly allows DSK to access YouTube Live data.

### Changed

- The settings menu now links directly to DSK privacy and terms, Google access
  revocation, and Twitch simulcasting terms.
- The public Kick destination badge is a neutral DSK-drawn glyph instead of a
  trademark image copied from a third-party repository.
- The installer now includes the GPL license, privacy policy, simulcast guidance,
  third-party notices, limited-beta safety guide, and a guarded user-data removal tool.

### Security

- Normal uninstall keeps current-user settings and credentials for reinstall;
  an explicit complete-removal choice deletes only DSK Multistream profile files,
  module settings, and `DSK Multistream/` Credential Manager entries.
- Complete-removal path and credential allowlists reject non-standard locations,
  other-product prefixes, and reparse-point traversal.

## [0.3.2] - Beta

### Fixed

- The YouTube `Split archives every 11 h 30 min` mode now persists after saving and reopening a target.
- Comment Viewer is notified only after YouTube confirms the selected broadcast is live, and receives that broadcast ID so comments follow the selected or newly rotated frame.
- A newly rotated archive no longer risks immediately splitting again when YouTube temporarily omits its actual start time.
- A failed next-archive start now stops at a bounded attention state after the current archive is confirmed complete instead of repeatedly retrying forever.

### Changed

- YouTube login now uses the dedicated DSK Multistream desktop OAuth application owned by the DSKC Google Cloud project.
- Release builds accept both the publisher OAuth configuration format and Google's downloaded Desktop client JSON format.

### Security

- Comment Viewer OAuth credentials are no longer candidates for Multistream release builds; each product keeps separate Google OAuth ownership and credentials.

## [0.3.1] - Beta

### Fixed

- Hiding or rearranging the Vertical Layout dock now suspends its preview without repeatedly destroying the private OBS video canvas, preventing a preview-lifecycle crash during dock visibility changes.
- Final plugin unload still releases the private preview canvas in the required remove-then-release order.

## [0.3.0] - Beta

### Changed

- The plugin, Windows installer, package manifest, and download filename now identify this release as 0.3.0 instead of reusing the 0.2.0 version.
- This release is the uniquely versioned distribution of the beta that includes YouTube archive switching every 11 hours 30 minutes.

### Security

- Twitch and Kick login now use the permanent `auth.dasoku.org` relay and the dedicated Multistream OAuth applications instead of the retired shared relay registration.

## [0.2.0] - Beta

### Added

- YouTube reusable stream discovery after login, with a named stream selector that fills the ingest server and stream key.
- Optional YouTube archive rotation mode that moves a continuous RTMP upload to a new broadcast before the 12-hour archive boundary.
- Automatic DSK Comment Viewer detection and OBS Comments Dock integration when the viewer is installed.

### Changed

- YouTube stream lookup failures no longer block login or manual stream-key entry.
- Existing manually entered YouTube stream keys are preserved when they do not match a returned reusable stream.

### Security

- YouTube API stream metadata is bounded and validated before use, and stream keys are excluded from selector labels and logs.

## [0.1.0] - Beta

### Added

- Independent horizontal and vertical multistream outputs for OBS Studio 32.x.
- Twitch and Kick publisher login, YouTube OAuth support, and manual RTMP targets.
- Built-in 9:16 vertical layout editor and preview dock.
- Drag-and-drop ordering for Vertical Scenes and Sources, with OBS source-type icons.
- Windows installer with in-place updates and complete plugin uninstallation.
- Windows Credential Manager storage for stream keys and OAuth credentials.

### Security

- Release workflow prepared for origin-verified Authenticode signing of both the plugin DLL and installer.
