# Multistream 0.5.1 Beta — release preparation, 2026-10-16

Status: source preparation; **not signed, installed or published**.

## Scope

The user selected Multistream itself for public-release preparation and signing.
This candidate contains independent Horizontal output from 0.5.0 and the
2026-10-15 Vertical/Horizontal settings-panel width correction. Comment Viewer's
new display-parts controls are a separate product change and are not bundled.

The earlier v0.5.0 tag and its frozen signing DLL (SHA-256
`071167C894DA092464C451E0C9F4C321B55E093DF8176C808AEB4323765B58EB`)
remain unchanged. Version 0.5.1 distinguishes the corrected input.

## Compatibility and recovery

Settings schema 2 and all authentication scopes, ports and credential ownership
remain unchanged. Preserve pre-upgrade profile settings when updating from
0.4.1; rollback requires the previous DLL and those settings, not a schema edit.
The existing signed public 0.4.1 artifact is retained.

## Planned fixed build

- Reviewed source revision, version 0.5.1.0.
- Pinned OBS 32.1.2 SDK and dependency archives from buildspec.json.
- Publisher release ON, E2E hooks OFF; use only the existing product-owned
  Multistream OAuth configuration without copying it into source or reports.
- All CTests, redacted source secret check, payload validation and Defender.
- Signing order: plugin DLL, generated Inno uninstaller, final Setup EXE.
- All three layers must match the independently pinned publisher certificate,
  have Valid Authenticode and trusted timestamps.

Source freeze, actual build and tests, staging paths and hashes will be recorded
when confirmed. Prior 0.5.0 and local-development evidence does not substitute
for checks of this new artifact.

## Remaining gates

Before publication, validate the final signed artifact's installation, update,
OBS load/normal exit, settings retention, complete removal and isolation from
other products. Native Comments rendering and restart checks remain incomplete
for the 2026-10-15 local developer build; do not report them as passed.
Do not alter the user's running OBS or installed apps as part of source/build
preparation. Public deployment and final clean-PC qualification are separate
from creating and signing the candidate.
