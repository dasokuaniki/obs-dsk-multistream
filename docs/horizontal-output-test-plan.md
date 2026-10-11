# Independent horizontal output — development test plan

Status: implemented and tested with an isolated OBS runtime. On 2026-10-10, a separate non-instrumented development build was installed into the user's existing Codex PC developer-plugin slot and its visible DSK Horizontal dock was verified. It remains unsigned and unpublished, and release qualification is incomplete. See [local installation and recovery record](horizontal-output-local-install-20261010.md).

On 2026-10-12, the 0.5.0 Beta publisher candidate passed CTest 30/30 and two native OBS 32.1.2 starts with restored Horizontal, Vertical, Streaming and Comments docks. The checks found and fixed selection reappearing after refresh and late dock registration losing saved placement. Independent Main/Horizontal switching, OBS Link on/off, narrow dock resizing and separate Setup preferences were exercised. Both final native exits reported zero unfreed allocations and no crash report. An isolated unsigned installer passed six lifecycle checks; this is not the final signed/public-AppId or clean-PC qualification. See [release check record](horizontal-output-release-check-20261012.md). Signed public 0.4.1 and the installed developer DLL have not been replaced by this candidate.

Verified on 2026-10-08: full Windows plugin build succeeded; CTest 28/28 passed, including the actual toolbar widget at narrow/wide widths and after font/theme changes. The PowerShell RTMP harness parses without errors. Eight staged payload files match their package manifest. In a build-local portable OBS 33.0.0-beta5 clone, decoded local RTMP frames confirmed separate Main/Horizontal and Horizontal/Vertical content. Both native previews displayed the corresponding compositions, and hiding a Horizontal source changed only its own preview and transmitted frames. Outputs stopped and OBS exited normally. The runtime still reports one unfreed allocation; its origin is unresolved. See [native canary evidence](horizontal-output-native-canary-20261008.md).

Current staged payload: `build/horizontal-dev-20261008/test-payload-verified/obs-dsk-multistream`. DLL SHA-256: `5583B83ACFD679FC04F851E6850E7FEFDC9D7DB966E2B203FC115DBAE7AF729A`. Staging does not register or install the plugin into OBS. The earlier `test-payload` remains preserved, but is not the current tested payload.

## Behavior

- OBS Main, DSK Horizontal, and DSK Vertical are three distinct output choices.
- OBS Main retains the legacy `dsk-horizontal` identifier and existing behavior.
- DSK Horizontal uses the new `dsk-horizontal-canvas` identifier, an independent 1920×1080 canvas, and a separate `DSK Horizontal` dock.
- Horizontal scenes, source ordering, transforms, Snap, Setup visibility, and OBS scene links are independent of DSK Vertical. Both editors use the same implementation.
- OBS Link is off initially. With it off, switching the OBS main scene does not select another DSK Horizontal scene.
- The horizontal preview shows the composition used for that canvas, before encoding. It is not proof that a platform received or displayed the same frame.
- Matching destinations on the same canvas may share a DSK encoder. Different canvases never share a video encoder. Audio still uses the existing OBS audio mix; per-canvas audio routing is not part of this change.
- Horizontal is a private, ephemeral canvas. The persistent DSK Vertical canvas and OBS native Twitch Dual Format selection remain unchanged.

## Settings compatibility

The new writer uses settings schema 2 and reads schema 1 and 2. The reader does not rewrite a file just to load it. Saves retain the existing atomic-write and `.bak` behavior.

Version 0.4.1 must not read a schema-2 file as a normal profile: an unknown canvas ID would otherwise become OBS Main. Its existing newer-schema protection instead blocks saving and does not enable those destinations. Before testing, preserve a separate copy of the OBS profile's `dsk-multistream.json`; `.bak` is only the previous save, not a permanent pre-upgrade backup. Rollback requires restoring the pre-test copy after OBS has stopped. Do not rewrite only the schema number or output ID.

## Local automated verification

Build directory: `build/horizontal-dev-20261008`. This instrumented development build has E2E hooks enabled and bundled OAuth disabled. It is suitable for offline UI/unit tests and local RTMP tests, not for testing publisher YouTube login or distribution.

CTest includes independent geometry/templates, scene CRUD, encoder profiles, output-ID round trips, schema-1 migration, schema-2 persistence/corruption recovery, localized destination selection, routing/lifecycle contracts, and the existing regression suite. Source-contract tests protect code structure but do not replace real OBS rendering/encoding tests.

## Native canary and remaining checks

Use an isolated test profile and test plugin environment. Do not replace the production plugin or run the harness while another OBS process is active. Use `scripts/run-obs-rtmp-e2e.ps1 -IsolatedPortable -CaptureFrames` with a portable OBS tree strictly under this repository's `build` directory. This mode uses the portable configuration root and does not back up, replace, or restore production APPDATA settings. The legacy non-portable mode still temporarily changes production user-state and must not be used for these feature canaries. The harness never force-terminates OBS on timeout.

Completed native checks: dock registration, initial preview/aspect-ratio display, initially cleared source selection, Main+Horizontal and Horizontal+Vertical local RTMP content, live Horizontal visibility isolation, output stop, and normal shutdown. An initial native toolbar paint/layout defect was corrected; the actual Qt widget also has a failing-then-passing regression test.

Remaining checks:

1. Main/Horizontal scene independence and mapped OBS Link follow on/off passed on 2026-10-12. Profile and scene-collection transitions remain separate native checks.
2. Test native source add/remove, crop/fit/fill, source and scene drag ordering, Snap, and deselection on empty preview space. Visibility has passed, but does not cover all editing actions.
3. Narrow Horizontal resizing (about 203 px with Setup off), floating Vertical/Comments movement and resizing, separate Setup states and two restored startups passed on 2026-10-12. The earlier 2026-10-08 resize attempt remains inconclusive, not retroactively successful. Verify other themes/font scales and multi-monitor placement separately.
4. Repeat two Horizontal destinations with matching and different encoder settings. Verify hardware encoders, audio/AV sync, and long-duration operation separately.
5. Verify Start All / individual stop, OBS native Twitch controls, profile reload, scene-collection cleanup, and settings round-trip. The clean no-DSK OBS 32.1.2 baseline exited normally with two reported allocations; both final 0.5.0 native runs reported zero. A contaminated OBS 33 beta fixture crashed even without DSK and is preserved as a failed experiment, not a successful baseline or a DSK attribution. These finite checks are not long-duration leak proof.
6. Publisher OAuth/E2E-off build preparation is complete for 0.5.0. Fix the source revision, sign DLL/uninstaller/installer, repeat final signed lifecycle and Defender checks, then publish through the existing DSK-Site project. No signed 0.4.1 artifact or public download URL has been changed.
