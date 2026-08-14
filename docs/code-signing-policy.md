# Code signing policy

DSK Multistream publishes a Windows installer only after all three executable
layers carry a publicly trusted Authenticode signature. The earlier SignPath
Foundation application was not approved. Any candidate missing one of these
signatures remains withheld from the public download page.

## Scope

Only release artifacts built from the `dasokuaniki/obs-dsk-multistream` repository by the repository's GitHub Actions workflow or from the matching reviewed release commit may be submitted for release signing. The workflow builds from pinned OBS Studio and OBS dependency archives whose SHA-256 hashes are stored in `buildspec.json`.

All executable layers are signed and timestamped by the same publisher:

1. `obs-dsk-multistream.dll` is built from the reviewed release commit, signed, and verified before packaging.
2. Inno Setup generates and signs the uninstaller before embedding it in the package.
3. The Inno Setup installer is built with the signed DLL and uninstaller, signed, and verified before publication.

Every release-signing request requires manual approval. A version tag may create
an explicitly named `unsigned-beta` artifact for private verification, but an
unsigned artifact is never described or published as a release. If a commercial
certificate or cloud signing service replaces SignPath, the same three-layer
signature verification and exact-source controls remain mandatory.

## Team roles

- Committer and reviewer: [@dasokuaniki](https://github.com/dasokuaniki)
- Approver: [@dasokuaniki](https://github.com/dasokuaniki)

Repository and signing-provider accounts used for release work must have multi-factor authentication enabled.

## Current signing route

The SignPath Foundation application was not approved. The current planned route
is a publicly trusted individual code-signing certificate enrolled in SSL.com
eSigner and loaded through eSigner CKA. The private key remains in the provider's
cloud HSM. `scripts/sign-windows-release.ps1` uses Windows SignTool to sign the
plugin DLL first and verifies its signer and RFC 3161 timestamp. It then gives
Inno Setup a fixed SignTool command so the generated uninstaller and final setup
executable are signed during compilation. The helper verifies the final setup
signature before writing the public SHA-256 file; installer E2E verifies the
installed uninstaller has the same signer and a trusted timestamp.

The script's `-PlanOnly` mode validates the clean DLL, disabled E2E hooks,
Windows SDK signing tool, version, and intended order without using a certificate
or changing an artifact. A real signing run requires the reviewed certificate to
be present in the current user's Windows certificate store through eSigner CKA.
Passwords, OTP seeds, and signing-provider credentials must never be passed to
this script, stored in the repository, or written to release logs.

## Privacy

See the project [privacy policy](privacy.md). DSK Multistream has no analytics or advertising and does not transfer information to networked systems unless requested by the user or required by a feature the user configured.

## Release controls

- Signing is restricted to version tags matching `v*`.
- GitHub-hosted Windows runners are used for every job leading to a signing request.
- Third-party GitHub Actions are pinned to full commit hashes.
- Source archives and OBS dependencies are verified against committed SHA-256 hashes.
- The plugin DLL and installer must expose matching product name and version metadata.
- The plugin DLL, generated uninstaller, and final installer must be signed by
  the same publisher certificate and carry trusted timestamps.
- The final SHA-256 checksum is generated only after all Authenticode signing.
- The installer provides an uninstaller and does not remove user profiles or credentials without an explicit user action.
