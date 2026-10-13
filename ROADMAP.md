# Development Roadmap

This roadmap captures the agreed next steps for Vita Moonlight PAF. It is ordered by risk reduction and release readiness, not by a promise that every item must be completed before development continues. Keep individual changes small and verify device-specific behavior on real PS Vita hardware.


## Recent streaming implementation review

The source-level changes for the following items are present in `master`; this is not a claim that they have passed a clean build or real-device testing:

- File-based debug logging remains removed. The `Debug log` setting has been restored as a console-only `psp2shell` toggle, default off.
- The stream page owns the periodic `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT)` task.
- `Swap X/O` applies only when custom mapping is disabled; custom mapping takes precedence.
- Video output dimensions are calculated for fit and center-crop modes. Center Region Only now copies the centered 960×544 crop in-place before queueing the frame to PAF; the decoder keeps its larger cover-sized pitch.
- The stream page has a dedicated opaque black 960×544 background plane behind the video surface, so any letterbox/pillarbox area shows black rather than the app's decorative background texture.
- Per-frame console logging was removed from the video presentation hot path.

**Important test interpretation:** Center Region Only is a crop-to-fill mode, not a zoom control for desktop UI elements. Vita's 960×544 aspect ratio (about 1.765:1) is very close to 1920×1080 (1.778:1), so that stream mode produces only a tiny crop and can look identical to Fit. For a visibly different test, use a host-supported stream mode with a substantially different aspect ratio (the Vita resolution list includes 1280×540, if the host accepts it), then compare Fit and Center Region Only. The PC desktop resolution alone is not the reference; use the actual negotiated stream dimensions.

**Still required:** build with the documented Vita toolchain and test on real hardware. Verify Fit and Center Region Only with multiple aspect ratios, stream start/stop/reconnect and failed startup, standby/display dimming behavior, and video smoothness with diagnostics enabled. Do not mark these items fully verified until those checks have actually run.


## 1. Stabilize the core streaming lifecycle

**Priority: first**

Run the full user journey on a real Vita and record reproducible failures with runtime logs before changing implementation.

- [ ] Discover a PC on the LAN and save/select it.
- [ ] Add a PC manually, including a custom port; verify unsuccessful attempts do not create an incorrect saved-host record.
- [ ] Pair successfully and verify incorrect-PIN errors are distinguished from transport/protocol failures.
- [ ] Retrieve the app list, start a stream, and check video, audio, controller input, and touchscreen input.
- [ ] Stop the stream, return to the app UI, and connect again; check that resources and callbacks are cleaned up safely.
- [ ] Exercise connection failure, host/network loss, recovery, and relevant PS Button/application lifecycle behavior.
- [ ] Verify gyroscope and accelerometer reporting on hardware; the input path exists but is not yet fully hardware-verified.

**Done when:** the main journey and important failure/recovery paths have been exercised on Vita, observed problems are either fixed or documented with reproduction steps, and stream shutdown/reconnect does not leave the app in a broken state.

## 2. Make development builds reproducible

**Priority: next, and can be worked on alongside device validation**

- [ ] Record the exact supported VITASDK revision.
- [ ] Record the exact revisions of `vitasdk-paf-component` and `psp2cxml-tool`.
- [ ] Document dependency setup and environment variables in a clean, repeatable setup procedure.
- [ ] Validate a clean build with `bash build.sh`, including CXML-to-RCO compilation and VPK/SELF generation.
- [ ] Document the expected toolchain and known build warnings; do not treat a build as verified unless it was actually run.

**Done when:** a fresh environment can follow the instructions to build the project without undocumented manual fixes.

## 3. Close technical risks and improve testability

- [ ] Investigate the `[Main] RefreshHosts ... after_insert=` log when reproducing host-list duplication; `after_insert` should equal `hosts`. Determine whether any mismatch comes from persistent records or stale PAF ListView cells before changing code.
- [ ] Identify small, hardware-independent logic suitable for tests, starting with host identity resolution, pairing-result classification, and host persistence decisions.
- [ ] Add focused automated tests for that logic where it can be isolated without mocking the whole PAF runtime.
- [ ] Select and document formatter/static-analysis tooling only if it fits the existing codebase and toolchain.

**Done when:** the known host-list diagnostic has an evidence-based conclusion and the highest-value non-PAF logic has repeatable checks where practical.

## 4. Prepare a release

- [ ] Review licensing and attribution obligations for this project, Moonlight code, and vendored dependencies before choosing or adding a project license.
- [ ] Document installation, runtime requirements, setup, and known compatibility limitations for the Vita and PC host.
- [ ] Define versioning, release artifact naming, and a release checklist.
- [ ] Perform a final clean build and smoke test on real hardware; verify core streaming and clean shutdown on the release candidate.
- [ ] Publish only artifacts and source revisions that have actually been validated.

**Done when:** licensing/attribution is addressed, users can build or install from documented instructions, and the release candidate has a recorded Vita smoke-test result.

## Working order

Start with **streaming lifecycle validation and motion-input verification**. Work on dependency pinning and reproducible builds in parallel when convenient. Use failures and logs from those runs to prioritize technical fixes; then add focused automated checks and finish with release preparation. Avoid broad speculative refactors or new system integrations until there is a concrete user-facing need.

## Scope boundaries

- Keep PAF UI concerns separate from services, backend, and legacy streaming code.
- Do not change the persistent host/settings formats or locations as incidental cleanup.
- Do not repeat broad native-system API surveys already recorded in `AGENTS.md`; investigate a system API only for a concrete use case.
- Treat hardware observations, build results, and test results as complete only after they have actually been verified.

## Follow-up hardware test results (2026-10-12)

User-reported Vita test results:
- Clean build and startup succeeded.
- No log file is created; periodic `[Video] decoded frame=...` messages still appeared in `psp2shell`.
- X/O Swap behaves correctly.
- Power Tick runs only during streaming.
- Center Region Only was initially reported visually incorrect: blank/letterboxed areas remained above and below, with black side bars.
- In a later test with 1920×1080, toggling Center Region Only did not visibly change the picture. The full desktop fits on Vita and UI elements look small; given the near-identical stream and Vita aspect ratios, this is expected to produce only a very small crop. Test with a substantially different negotiated stream aspect ratio before treating this as an implementation failure.
- Initially, the selected stream resolution appeared not to reach the host. Follow-up testing suggests the selected resolution was unavailable on the PC; choosing a resolution supported by the host resolved the issue.
- Holding PS to open Quick Actions does not suspend controller forwarding to the host.

Follow-up source changes:
- Restored the `Debug log` setting as a console-only toggle (default off); it gates `vita_debug_log()` and never writes a file.
- Replaced oversized-plane clipping with an in-place centered framebuffer crop matching the original Vita Moonlight texture-region logic. This avoids an extra framebuffer allocation but adds a per-frame memory copy; it needs a clean build and hardware/performance retest.
- Compared the crop behavior with the original `xyzz/vita-moonlight` implementation (`src/video/vita.c`, branch `vita`): `update_scaling_settings()` computes an oversized texture and a centered source rectangle, then `draw_streaming()` calls `vita2d_draw_texture_part()` to draw only that rectangle at the screen origin. It does not change the host desktop resolution; geometry is based on the negotiated stream width/height.
- Added a diagnostic line before stream startup reporting the selected width, height, FPS, bitrate, and graphics-optimization flag. With Debug log enabled, use this to determine whether the chosen resolution is read from AppSettings before the host launch request.

Still unresolved / required:
- Hardware verification now confirms the expected Fit/center-crop difference at 1280×540. Still build and retest with 16:9, 16:10, 4:3 and ultrawide *stream aspect ratios* where supported. Use the negotiated stream dimensions, not the host desktop setting, as the reference, and verify the in-place crop does not regress frame pacing. The original uses a centered source rectangle (`vita2d_draw_texture_part`); the PAF port emulates this by copying the centered 960×544 crop into the start of the framebuffer.
- Confirm stream resolution selection for host-supported and host-unsupported modes; compare `[GameStream] selected stream mode=...` with the host's actual negotiated/display mode. If the diagnostic values are correct but the host ignores a supported mode, investigate Sunshine's launch/resolution policy rather than changing the Vita decoder size.
- Research a supported shell/app event or status signal for Quick Actions. Do not claim the overlay is detectable until an API is verified. Avoid a timer-only input suppression workaround that could resume forwarding while the overlay remains open.
- Rebuild CXML and the app, then retest on real hardware; these latest source changes have not been built or device-verified by the current agent.
