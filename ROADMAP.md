# Development Roadmap

This roadmap captures the agreed next steps for Vita Moonlight PAF. It is ordered by risk reduction and release readiness, not by a promise that every item must be completed before development continues. Keep individual changes small and verify device-specific behavior on real PS Vita hardware.


## Recent streaming implementation review

The source-level changes for the following items are present in `master`; this is not a claim that they have passed a clean build or real-device testing:

- File-based debug logging and its UI setting were removed; diagnostics remain on `psp2shell`.
- The stream page owns the periodic `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT)` task.
- `Swap X/O` applies only when custom mapping is disabled; custom mapping takes precedence.
- Video output dimensions are calculated for fit and center-crop modes. The stream plane is centered within the 960×544 viewport when its surface exceeds the screen.
- Per-frame console logging was removed from the video presentation hot path.

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
- Center Region Only is still visually incorrect: blank/letterboxed areas remain above and below, with black side bars.
- The selected stream resolution does not appear to reach the host, while FPS and bitrate do.
- Holding PS to open Quick Actions does not suspend controller forwarding to the host.

Follow-up source changes:
- Restored the `Debug log` setting as a console-only toggle (default off); it gates `vita_debug_log()` and never writes a file.
- Corrected center-crop plane positioning to use PAF's center-origin coordinates: `SetPos(0, 0)` centers the oversized surface. This still needs hardware retesting.
- Added a diagnostic line before stream startup reporting the selected width, height, FPS, bitrate, and graphics-optimization flag. With Debug log enabled, use this to determine whether the chosen resolution is read from AppSettings before the host launch request.

Still unresolved / required:
- Retest Center Region Only with 16:9, 16:10, 4:3 and ultrawide host modes; compare against the original `xyzz/vita-moonlight` crop-region logic.
- With Debug log enabled, verify `[GameStream] selected stream mode=...` matches the selected AppSettings value and inspect the host's actual negotiated/display mode. The source already puts width/height in both the GameStream `mode=WxHxFPS` launch query and `STREAM_CONFIGURATION`; if the diagnostic values are correct but the host ignores them, investigate Sunshine's launch/resolution policy rather than changing the Vita decoder size.
- Research a supported shell/app event or status signal for Quick Actions. Do not claim the overlay is detectable until an API is verified. Avoid a timer-only input suppression workaround that could resume forwarding while the overlay remains open.
- Rebuild CXML and the app, then retest on real hardware; these latest source changes have not been built or device-verified by the current agent.

