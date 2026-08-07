# Code signing policy

DSK Multistream publishes a Windows installer only after both executable layers
carry a publicly trusted Authenticode signature. The earlier SignPath Foundation
application was not approved, so the current unsigned candidate is withheld from
the public download page while a trusted signing identity is arranged.

## Scope

Only release artifacts built from the `dasokuaniki/obs-dsk-multistream` repository by the repository's GitHub Actions workflow or from the matching reviewed release commit may be submitted for release signing. The workflow builds from pinned OBS Studio and OBS dependency archives whose SHA-256 hashes are stored in `buildspec.json`.

Both executable layers are signed:

1. `obs-dsk-multistream.dll` is built, submitted to SignPath, and verified before packaging.
2. The Inno Setup installer is built with that signed DLL, submitted separately, and verified before publication.

Every release-signing request requires manual approval. A version tag may create
an explicitly named `unsigned-beta` artifact for private verification, but an
unsigned artifact is never described or published as a release. If a commercial
certificate or cloud signing service replaces SignPath, the same two-layer
signature verification and exact-source controls remain mandatory.

## Team roles

- Committer and reviewer: [@dasokuaniki](https://github.com/dasokuaniki)
- Approver: [@dasokuaniki](https://github.com/dasokuaniki)

Repository and SignPath accounts used for release work must have multi-factor authentication enabled.

## Privacy

See the project [privacy policy](privacy.md). DSK Multistream has no analytics or advertising and does not transfer information to networked systems unless requested by the user or required by a feature the user configured.

## Release controls

- Signing is restricted to version tags matching `v*`.
- GitHub-hosted Windows runners are used for every job leading to a signing request.
- Third-party GitHub Actions are pinned to full commit hashes.
- Source archives and OBS dependencies are verified against committed SHA-256 hashes.
- The plugin DLL and installer must expose matching product name and version metadata.
- The final SHA-256 checksum is generated only after Authenticode signing.
- The installer provides an uninstaller and does not remove user profiles or credentials without an explicit user action.
