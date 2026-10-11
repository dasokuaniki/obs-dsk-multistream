# Multistream 0.5.0 Beta — release preparation, 2026-10-12

Status: **unsigned candidate; not published**. Public signed 0.4.1 remains
unchanged. This record must not be used as final signed release qualification.

## Scope and compatibility

Independent DSK Horizontal is a 1920x1080 canvas beside OBS Main and DSK
Vertical. It has separate scenes, source order/transforms and OBS links;
the preview represents that canvas before encoding. Audio uses the existing
OBS audio mix. Per-canvas audio routing is not included.

Settings schema 2 reads schemas 1 and 2. Downgrading to 0.4.1 requires the
pre-upgrade DSK settings backup as well as the old plugin. Do not rewrite only
the schema number/output identifier. Existing Twitch Dual Format remains
OBS-native, using the existing Twitch controls and persistent Vertical canvas.

## Fixes discovered during release checks

1. `refreshItems()` selected the first row after a cleared selection and an
   OBS scene notification. A bounded failing regression check preceded removal
   of that fallback. A cleared preview selection now remains cleared.
2. DSK docks registered after `FINISHED_LOADING` missed OBS's window-state
   restoration. An attempted late `restoreDockWidget` fix failed actual Qt
   checks because OBS's add/hide/float sequence replaced the saved placeholder.
   The final implementation registers empty shells synchronously in module
   post-load, before OBS restores the window. Controls, canvases and CEF still
   initialize after `FINISHED_LOADING`. It does not restore the entire window
   again, force other docks' sizes, or use Qt private APIs.

Two new regression tests cover real Qt docked/floating/hidden restoration,
geometry and late content loading, plus the startup-phase contract. Existing
comment-browser localhost navigation, isolated connection pool and async CEF
initialization are preserved.

## Build and automated checks

- Windows MSVC 19.50.35729 / Ninja, Qt 6.8.3 and the existing OBS SDK.
- Build: `build/windows-release-0.5.0-20261012`, version 0.5.0.0.
- Publisher-release enabled, E2E hooks disabled, approved product-owned desktop
  OAuth configuration supplied privately. No scopes, redirect or credential
  sharing boundaries were changed.
- CTest **30/30 passed**, 13.27 seconds. The earlier 28/28 run is separate.
- DLL size: 2,427,392 bytes. Microsoft Defender custom scans of the current
  staged payload and unsigned E2E installer both exited 0 with no threats.
  Final signed files still require their own scans.
- DLL SHA-256 before signing:
  `071167C894DA092464C451E0C9F4C321B55E093DF8176C808AEB4323765B58EB`.
- A build without the MSVC environment failed to locate `type_traits`;
  rebuilding under `vcvars64.bat` succeeded. This was not a product-code error.
- Home-server Gitleaks 8.30.1 scanned the source-only snapshot (4.28 MB,
  182 files including private routing metadata). One finding was reviewed as
  the existing synthetic `publisher_secret_1234567890` parsing-test fixture at
  `tests/oauth-config-format-test.cmake:11`, not a credential. An exact audit
  baseline excludes only this reviewed finding; it is not a repository-wide
  suppression. Public source excludes local routing files and server tools.

## Native OBS checks

All feature checks used a repository-local portable OBS with synthetic color
sources and test settings, not the user's real stream keys. No external
broadcast, recording, virtual camera or comment submission was started.

Official OBS 32.1.2 Windows x64 ZIP: 184,814,726 bytes, SHA-256
`8D97E4563BD8D22D03E63042AA7DCCCEDE1D555C9BD35CE8A9E5019B0D0201BF6`.
The OBS executable's original OBS Project LLC signature was valid and was
not replaced. The renderer is `Direct3D 11` (the earlier `Direct3D11` fixture
value wrongly selected OpenGL).

Native portable path: `build/horizontal-release-check-20261012/obs-stable-horizontal-0.5.0`.
Relevant successful final logs: `2026-10-12 03-40-33.txt` and
`2026-10-12 03-50-37.txt`, under that runtime's `config/obs-studio/logs`.

Verified visually and in runtime logs:

- Main and Horizontal scenes switch independently with OBS Link off; mapped
  Main scenes select the corresponding Horizontal scene when follow is on.
  Unmapped Main scenes retain the active Horizontal scene.
- Source add and empty-space deselection; selection stayed cleared after Main
  scene changes instead of restoring the white frame.
- Horizontal narrowed to approximately 203 px with Setup off; its preview
  kept 16:9 letterboxing. Vertical Setup on and Horizontal Setup off remained
  independent.
- Floating Vertical and Comments docks could be moved/resized. Horizontal's
  docked right placement, floating dock geometry and all four docks' visible
  states survived the second startup without resetting the test configuration.
- Comment Viewer API v1 discovered version 0.2.0-beta.148 at local port 17321.
  Browser navigation and `DSK Comment Viewer` title appeared on both starts;
  platform selector, Support counters and send controls rendered, not a white
  page. No actual incoming chat or comment post was tested.
- Both final runs exited normally, reported `Number of memory leaks: 0`, and
  generated no crash report. This is not a long-duration leak-free guarantee.

Earlier experiments are retained, including:

- A stale OBS 33.0.0-beta5 no-DSK fixture crashed on normal close in source-tree
  cleanup (`pthread_mutex_unlock` / `signal_handler_disconnect`). A fresh
  minimal no-DSK 33 fixture exited normally; the contaminated fixture is not
  evidence that DSK caused the crash.
- A clean no-DSK 32.1.2 baseline exited normally and reported two allocations.
- Source/scene drag ordering in native OBS was inconclusive; an escaped drag
  ghost and unchanged stored order are not a passing result.
- The first Sky launch opened installed OBS instead of the intended portable
  binary; it was closed without starting any output. Later launches used an
  explicit executable/work directory and verified the loaded module path.
  Installed developer DLL SHA was rechecked unchanged. Full production-config
  pre/post equality was not established and is not claimed.

The 2026-10-08 instrumented local RTMP decoded Main/Horizontal and
Horizontal/Vertical checks remain evidence for that older build, not a test
of this new DLL. See the separate native canary record.

## Isolated installer lifecycle

The second, current unsigned E2E installer uses a separate test AppId and a
repository-local destination. It is **not for distribution**:

- `build/horizontal-release-check-20261012/installer-e2e-package-r2/DSK-Multistream-0.5.0-Windows-x64-E2E-Setup.exe`
- 2,612,120 bytes; SHA-256
  `19D850328CCBB44D106B3B49399B0198335C33EB2D9A946F5F49839E6417F545`.
- 14 manifest payload files; DLL SHA matches the publisher candidate; no user
  settings included. Test packaging does not enable plugin E2E hooks.
- Fresh install, same-version overwrite, uninstall, settings preservation,
  complete removal and other-product preservation: **all six passed**.
- Five lifecycle logs retained under
  `build/horizontal-release-check-20261012/installer-lifecycle-e2e-r2`.

The first E2E package and its six passing lifecycle checks are preserved but
superseded because they preceded the dock fix. Neither package validates the
final signed public AppId, a 0.4.1-to-0.5.0 upgrade, public UAC installation or
a clean GAME PC.

## Remaining public-release gates

1. Review and fix the exact source revision; exclude unrelated local AGENTS,
   backups and server-signing tools. Run the available redacted secret check.
2. Sign DLL, generated Inno uninstaller and final Setup in that order, through
   the established home-server eSigner runner. User password/OTP entry remains
   interactive; do not store or reuse secrets.
3. Validate all three signatures against the independently pinned signer,
   trusted timestamps, final hashes and Defender scans.
4. Repeat final signed lifecycle, actual OBS load/exit and cross-version
   settings/upgrade checks in the approved Windows test environment.
5. Update only the existing DSK-Site project, preserving old signed releases;
   download the new public file and recheck size/hash/signatures.

Hardware encoders, AV sync, endurance, all transform/drag actions, other theme
scales and current external-service delivery are not certified by these
finite offline checks. Publish only as Beta after the required gates, with
these boundaries and rollback instructions retained.
