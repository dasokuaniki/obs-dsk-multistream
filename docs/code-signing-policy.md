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

### Publisher release gates (0.4.1 and later)

Configure official builds with `scripts/configure-windows.ps1 -PublisherRelease`
and the existing publisher-only `-OAuthAppConfig` input. This enables
`DSK_PUBLISHER_RELEASE=ON`, which rejects missing YouTube OAuth configuration
and E2E hooks. Generic OSS builds retain the opt-out custom-client workflow.
The release UI test additionally verifies that bundled login is available.

Pass `-ExpectedSignerThumbprint` to every external-signing phase. Obtain this
public certificate thumbprint from an independently approved publisher
certificate, never from the candidate being verified. Every signed layer must
match it and have a valid signature and trusted timestamp. Certificate renewal
requires deliberate approval of the replacement thumbprint.
`StagePlugin` and `-RequirePublisherRelease` packaging also check the generated
OAuth header; keep that header private to the local build/staging tree.

The SignPath Foundation application was not approved. The current route uses a
publicly trusted individual code-signing certificate in SSL.com eSigner. The
private key remains in the provider's cloud HSM. Signing runs interactively on
the private home server with SSL.com's CodeSignTool; passwords, OTP values,
credential identifiers, and private-key material are not stored in the source
tree, process arguments, release artifacts, or persistent logs.

`scripts/external-windows-signing.ps1` controls four Windows-side phases. It
stages the reviewed unsigned plugin DLL, asks Inno Setup to create its stable
external `.e32` uninstaller artifact, builds the installer after both embedded
executables have been signed, and finally verifies all three signatures and
RFC 3161 timestamps before writing the release SHA-256 file. Inno Setup's
official `SignedUninstallerDir` two-pass flow preserves the external uninstaller
signature when it is embedded. Each of the plugin DLL, `.e32` uninstaller, and
final installer is transferred to the private server and signed only after the
preceding phase passes. Installer E2E then verifies that the installed
`unins000.exe` has the same signer and a trusted timestamp.

The older `scripts/sign-windows-release.ps1` Windows SignTool route remains an
optional compatibility path for a working CKA installation, but it is not the
active release route. A CKA provider failure must never be bypassed by publishing
an unsigned layer.

## Inactive SignPath CI route

### Reapplication preparation (2026-10-21)

A renewed SignPath Foundation application is being prepared. The previous
application was not approved, and preparation does not establish approval,
an active subscription, or completed signing. Public version 0.5.1 remains the
existing SSL.com-signed release; this work does not replace its files or tag.

The intended request covers future official Windows releases built from this
repository. Onboarding must confirm the generated Inno Setup uninstaller's
eligibility and `.e32` acceptance, origin verification, per-request manual
approval, account MFA, and the independently approved certificate identity.
No current account MFA state or provider permission is inferred from this policy.
The project remains subject to the [Foundation conditions](https://signpath.org/terms.html)
and [application review](https://signpath.org/apply.html).

If Foundation signing is approved and activated, the corresponding release and
download pages will use its required attribution: "Free code signing provided
by SignPath.io, certificate by SignPath Foundation". That attribution does not
describe the existing SSL.com-signed downloads.

### Activation and verification

The legacy SignPath route stays inactive unless the repository variable
`SIGNPATH_RELEASE_SIGNING_APPROVED` is explicitly `true`. Existing credentials
alone do not reactivate it. No repository or provider settings are changed by
this workflow correction.

Before approving activation, configure the dedicated uninstaller artifact
configuration from `signing/signpath-uninstaller.xml` using
`SIGNPATH_UNINSTALLER_ARTIFACT_CONFIGURATION_SLUG`, and independently approve
`SIGNPATH_EXPECTED_SIGNER_THUMBPRINT`. All existing SignPath inputs and the
product-owned publisher OAuth configuration are also required. An incomplete
approved configuration fails before any signing request.

CI reuses the existing external-signing phases in this order: StagePlugin,
DLL signing, PrepareUninstaller, `.e32` signing, BuildInstaller, Setup signing,
then VerifyRelease. Returned files must be unique, retain the unsigned input's
version metadata, and have Valid signatures, the approved signer and trusted
timestamps. Copies must retain their hashes. The uninstaller's Inno FileVersion
is distinct from the product version, so its XML does not constrain FileVersion.

Actual SignPath service acceptance of `.e32` is not yet qualified. It requires
provider approval and an isolated end-to-end check before activation; no manual
extension-renaming fallback is permitted. Any provider or verification failure
stops the job. Only successful three-layer verification produces a
`signed-candidate` artifact. It is not a published release: Defender, fresh
install, upgrade, startup, normal and complete removal, other-product protection
and official-download checks remain separate mandatory release gates.

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
