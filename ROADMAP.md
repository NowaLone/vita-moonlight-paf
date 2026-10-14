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

- [x] Verify the `RefreshHosts` cell-count fix on Vita: user confirmed the host list behaves correctly. Reported logs show `after_delete=0` and `after_insert=hosts` across repeated refreshes, selection-mode toggles, and removing one of two hosts. The user reports the remaining list behavior is OK. A clean build was not separately verified. Source review found that `DeleteCell` was passed `existing - 1` even though BetterHomebrewBrowser passes the full `GetCellNum(0)` count.
- [ ] Identify small, hardware-independent logic suitable for tests, starting with host identity resolution, pairing-result classification, and host persistence decisions.
- [ ] Add focused automated tests for that logic where it can be isolated without mocking the whole PAF runtime.
- [ ] Select and document formatter/static-analysis tooling only if it fits the existing codebase and toolchain.

**Done when:** the known host-list diagnostic has an evidence-based conclusion and the highest-value non-PAF logic has repeatable checks where practical.

## PS Button capture and Quick Actions lifecycle
### Log observations (2026-10-14)

- Device log confirms the input loop logs PS rising edges and does not stop the stream on the observed presses.
- The supplied excerpt does **not** include `[Input] PS Button lock result=...`; therefore it does not establish whether `sceShellUtilLock(PS_BTN | PS_BTN_2)` succeeded.
- The excerpt also contains Limelight video recovery messages: unrecoverable frames, missing IDR frames, and a 16-frame network-drop report. These indicate loss of enough frame data to decode/recover and recovery waiting; they do not alone prove whether the root cause is network loss, application suspension, or decoder scheduling.
- Next diagnostic: capture the startup lines including `[Input] start ... ps_capture=...` and `[Input] PS Button lock result=...`, plus timestamped logs immediately before/after PS press and Quick Actions return. Compare the same stream without pressing PS. Keep input/lifecycle diagnosis separate from video packet-loss diagnosis until there is correlated evidence.



**Status: blocked on SELF authorization verification and real-device lifecycle testing.**

- The current input code calls `sceShellUtilLock(PS_BTN | PS_BTN_2)` when the existing `ps_button_capture` setting is enabled, and logs the return code. The user observed that pressing PS still minimizes/suspends the app; therefore the input-side double-press logic alone does not satisfy capture.
- Upstream `xyzz/vita-moonlight` packages its SELF with `vita-make-fself -a 0x2800000000000001 -at 0x0E -c` and documents the SceShell permission as a prerequisite for PS capture. The current PAF project uses auth ID `0x2F00000000000101`, `vita_create_self(... UNSAFE)`, and SFO attributes `ATTRIBUTE=33472` / `ATTRIBUTE_MINOR=16`. In VitaSDK, `UNSAFE` omits the default safe flag; it does not replace the SELF authorization ID.
- Do **not** replace the current authorization ID or add SELF attributes solely by analogy with upstream: this project is a PAF/system-UI application, and the compatibility of those upstream SELF parameters with its system-UI launch/permissions has not been established. Resolve the required authorization/attribute combination before changing packaging.
- Keep the existing `ps_button_capture` setting as the sole toggle. When enabled, a single PS press should be sent to the host and only a confirmed double press should stop the stream. When disabled, do not intercept PS or stop the stream.
- Quick Actions input suspension still needs a verified system-state signal. `SceAppMgrAppState.isSystemUiOverlaid` is a candidate for investigation, not yet hardware-verified. Do not substitute a timer-only suppression window or lock Quick Menu unconditionally, since that can break native behavior when capture is disabled.
- Next verification: inspect the generated SELF metadata/permissions from a real build; test lock return codes and PS behavior on Vita with capture enabled and disabled; then validate the chosen AppMgr overlay signal through Quick Actions open/close and confirm controller forwarding resumes only after returning to the app. Record firmware and logs. Do not mark this complete before device testing.

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


## Host ListView diagnosis (2026-10-13)

Source review identified a likely off-by-one bug in `Main::RefreshHosts()`:

- The old code called `DeleteCell(0, 0, existing - 1)`. PAF's third argument is used as a deletion count in the project's reference implementation, so this can leave one old cell behind. Re-inserting `m_host_count` cells can then produce `after_insert = hosts + 1`, matching the duplicate-row symptom.
- BetterHomebrewBrowser's `AppBrowser::ClearList()` uses `DeleteCell(0, 0, listView->GetCellNum(0))`, passing the full cell count. The fix branch now follows that pattern and adds a comment documenting the count semantics.
- NetStream's server browser creates its ListView segment and item factory once, then inserts cells for the loaded entries. It does not provide evidence for deleting a range with an inclusive final index.
- The diagnostic `cells_after_delete` and `after_insert` log remains in place to validate the behavior on-device.

**Verification status:** source comparison only. No Vita toolchain build or hardware test has been run for this change. Before marking the item complete, verify empty → one host → multiple hosts, repeated refresh, and entry/exit from selection mode; require `after_delete=0` and `after_insert=hosts` in each refresh.


## Host ListView hardware test results (2026-10-13)

User-reported `psp2shell` logs from Vita after applying the `DeleteCell(0, 0, existing)` fix:

- Repeated refreshes with two hosts consistently report `cells before=2 after_delete=0 after_insert=2`.
- Selection mode toggles correctly rebuild the two rows: `selection=1` shows non-null checkbox pointers for both cells; `selection=0` shows `checkbox=0x0`.
- After removing `fedora43-1-common.local`, refresh reports `hosts=1 cells before=2 after_delete=0 after_insert=1`; a subsequent refresh remains at one cell.
- These observations support the fix for stale/duplicate ListView cells and the selection-template rebuild path.

**Follow-up:** user confirmed that the list behavior is OK. The clean build remains unverified separately; the runtime logs came from a running Vita build.


## Next task: Quick Actions, PS Button, and application lifecycle

**Priority: next, beginning with investigation rather than speculative API changes.**

Known issue from Vita testing: holding PS to open Quick Actions does not currently suspend controller forwarding to the streaming host. PS Button handling must respect the existing setting that controls forwarding PS to the PC; do not replace this setting with unconditional interception or unconditional system-button locking.

Required behavior:
- **PS forwarding setting enabled:** a single PS press is forwarded to the PC as the special/home button and must never stop or close the video stream. Only a confirmed double press may stop the stream; implement the original Vita Moonlight timing/state machine rather than treating a press edge as a stop. Coordinate this with Quick Actions and lifecycle handling so unrelated controller forwarding is paused while the system UI is active, then resumes only after a verified return to the app.
- **PS forwarding setting disabled:** do not intercept or consume PS, do not stop the stream on either a single or double press, and do not lock out the system action. Preserve Vita's native PS behavior, including suspending/minimizing the app to LiveArea.
- In both modes, stream stop, failed startup, app suspension/resume, and exit must not leave controller input forwarding stuck or stream resources in an inconsistent state.

Investigation checklist:
- [ ] Find the existing PS forwarding setting, confirm its persisted/default value, and trace how it currently affects controller input.
- [ ] Trace controller input from the Vita input reader through the Moonlight input sender and identify where conditional PS interception and safe pause/resume belong.
- [ ] Trace application and stream lifecycle entry points, including PAF runtime startup/shutdown, stream start/stop, page transitions, suspend/resume, and cleanup of worker threads/resources.
- [ ] Compare concrete lifecycle and PS Button handling in NetStream and BetterHomebrewBrowser; record which mechanisms are directly applicable to this PAF app and which are app-specific.
- [ ] Verify whether a supported shell/app event or status API signals Quick Actions visibility or foreground/background transitions. Document the API and evidence before relying on it.
- [ ] Define and test the enabled/disabled setting behavior for opening Quick Actions, returning to the app, entering LiveArea, resuming, stopping a stream, and exiting the app.
- [ ] Implement only after the event/ownership model is established; avoid timer-only suppression because it can resume forwarding while Quick Actions is still open.
- [ ] Build and verify on real Vita hardware with `psp2shell` logs. Record results and any firmware-dependent behavior here.

**Acceptance criteria:** with capture enabled, a single PS press is forwarded and never stops the stream; only a confirmed double press stops the stream. With capture disabled, PS retains native Vita behavior and neither press sequence is intercepted by Moonlight. Input forwarding resumes only after a verified return-to-app transition; stream and app resources remain valid through suspend/resume and are released safely on stop/exit. No unverified system API or hardware behavior is marked complete.

### Investigation findings (2026-10-13; source review, not hardware verification)

1. **The setting and current behavior are now identified.** `ps_button_capture` is already defined in `cxml/moonlight_settings.xml` with default `1`. `moonlight_stream_start()` passes it to `vita_input_configure()` as `enable_psbutton_capture`.
2. **The current enabled path violates both PS requirements.** In `src/moonlight/vita_input.c`, enabled capture acquires ShellUtil locks for `PS_BTN` and `PS_BTN_2`; any rising edge of `SCE_CTRL_PSBUTTON` immediately calls `moonlight_stream_stop()` and clears the bit. No PS/special button flag is sent to the host. This means one tap closes the stream and cannot be forwarded. A single tap must instead be forwarded as `SPECIAL_FLAG`; only a confirmed double tap may stop the stream, and only when capture is enabled. No code behavior has been changed in this analysis step.
3. **The disabled path must stay native.** When the setting is false, the lock and special PS handling should both be skipped. Do not consume PS, stop the stream, or lock the Quick Menu in this mode.
4. **Upstream behavior is now verified in source.** `xyzz/vita-moonlight` 0.13.2 README describes optional PS capture: a single press is forwarded to Sunshine as a PS/Xbox button mapping and a double press opens the menu. In `src/input/vita.c::handle_psbutton()`, `PSBTN_DOUBLETAP_DELAY` is 250,000 µs; a non-double press sends `SPECIAL_FLAG` through the special-button handler, while a double press unlocks the PS button to open the menu. There is no single-press stream stop in this path. `moonlight-common-c` defines `SPECIAL_FLAG`, not a distinct `PS_FLAG`. For this project, retain the upstream timing/state-machine approach but implement the requested local action of stopping the stream on a double press, including press/release handling and balanced shell locks.
5. **A promising Quick Actions/foreground signal has been found, but is not hardware-verified.** The VitaSDK `SceAppMgrAppState` structure includes `isSystemUiOverlaid`, and the documented `__sceAppMgrGetAppState()` / `_sceAppMgrGetAppState()` API family reports the calling app's pending-event and overlay state. `sceAppMgrReceiveSystemEvent()` can also deliver ON_ACTIVATE, ON_DEACTIVATE, ON_RESUME, and REQUEST_QUIT events. Confirm the exact symbol/signature in the project's installed VitaSDK headers, then test whether holding PS to open Quick Actions sets `isSystemUiOverlaid` on the target firmware. The input worker currently polls every 2 ms while `s_input_active` and has no overlay/foreground gate. `sceShellUtilLock()` is a shell-event lock, and `SCE_SHELL_UTIL_LOCK_TYPE_QUICK_MENU` is a separate lock type; neither should be treated as an overlay-state query. Do not add a timer-only gate or lock Quick Menu without confirming the required behavior.
6. **Lifecycle entry points need a separate trace.** `src/app/paf_runtime.cpp` sets the PAF plugin `stop_func` and `exit_func` to `NULL` and calls `MoonlightApp::Shutdown()` only after `Framework::Run()` returns. Verify whether PAF/AppMgr/ShellUtil offers a reliable suspend/resume or foreground event in this application context, and ensure input forwarding resumes only after a confirmed return.
7. **Reference-project comparison:** BetterHomebrewBrowser's `sceShellUtilLock(PS_BTN)` usage is scoped around blocking operations such as downloads/icon refreshes and is paired with unlock on success/failure. It is evidence for balanced lock/unlock cleanup, not for detecting Quick Actions. The project’s existing NetStream notes establish the PowerTick pattern only; no verified Quick Actions lifecycle mechanism has been identified there yet.

**Next implementation plan (after source/API verification):**
- Port the upstream PS press mapping and double-press behavior while preserving the existing setting and its default/storage.
- Keep all PS interception and ShellUtil PS locks conditional on `ps_button_capture`; disabled means native shell behavior.
- Add a small, explicit input-forwarding gate driven by a verified system/app lifecycle signal, not elapsed time alone. When gated, release any host-side held buttons once and avoid sending further controller/touch/motion events; reopen the gate only on confirmed foreground return.
- Make lock acquisition/release idempotent and cover stream startup failure, normal stop, page teardown, app exit, and any confirmed suspend/resume callbacks.
- Verify with logs and real hardware in both setting states: single press, double press, hold for Quick Actions, return to app, PS-disabled LiveArea behavior, stream stop/reconnect, and repeated transitions. Build and hardware verification remain outstanding.

### PS single/double press implementation update (2026-10-13)

Source change committed on `feat/quick-actions-lifecycle-review`:

- `src/moonlight/vita_input.c` now adds `SPECIAL_FLAG` to the host controller report while PS is held and capture is enabled.
- A single PS press no longer calls `moonlight_stream_stop()`; the log message now states that the button is being forwarded to the host.
- The input path records the PS release timestamp. A subsequent press within 250,000 µs (250 ms) is treated as a double press and requests stream stop. The second PS bit is cleared before the changed controller state is sent.
- The release timestamp is reset when input starts and stops.
- The existing setting and its default/storage were not changed. The existing ShellUtil PS locks remain conditional on capture being enabled, and disabled capture does not enter the PS interception branch.

**Verification status:** source change only. No build or Vita hardware test has been run for this commit. Verify on device that a single press reaches the PC as the special/home button and leaves the stream running; a double press stops the stream; a slow second press does not stop it; and capture-disabled PS still invokes native Vita behavior.

**Still unresolved:** this change does not implement Quick Actions overlay detection or pause/resume gating for other controller inputs. Do not consider the lifecycle requirement complete until a reliable system/app signal is verified on the target firmware and input forwarding resumes only after confirmed return to the app.

### ShellUtil initialization follow-up (2026-10-14)

The next device log reports `sceShellUtilLock()` as `0x80101801` even with `ps_capture=1`. The SELF packaging experiment therefore has not demonstrated successful PS-button locking in the reported run.

A source review found a second concrete prerequisite worth testing: Vita homebrew code in [TheOfficialFloW/h-encore](https://github.com/TheOfficialFloW/h-encore/blob/6d43b4cd662d1012ca851a86d43f3696b2323200/bootstrap/bootstrap.c) calls `sceShellUtilInitEvents(0)` before `sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN)`. Another Vita implementation documents that the ShellUtil event subsystem must be initialized before lock/unlock. The current PAF code did not initialize it.

**Change committed on `feat/quick-actions-lifecycle-review`:** `src/moonlight/vita_input.c` now calls `sceShellUtilInitEvents(0)` before attempting the PS-button lock, logs the initialization result, and retries initialization on a later stream start if the call fails. This is a hypothesis-driven change, not a confirmed fix; no build or device test has run for this commit.

**Next device test:** build/install this exact revision and capture both `[Input] ShellUtil init events result=...` and `[Input] PS Button lock result=...`. If init succeeds but lock still fails, investigate the exact error/SELF permissions next. If init fails, record its result and Vita firmware/CFW details. Do not infer that the lock works merely because the PS press handler logs a message.

### Follow-up device log (2026-10-14)

The new startup excerpt resolves the missing diagnostic:

- `[Input] start ... ps_capture=1` confirms the setting is enabled in the running input loop.
- `[Input] PS Button lock result=0x80101801` is **not success**; VitaSDK documents `sceShellUtilLock()` as returning 0 on success and a negative error on failure. The PS capture path therefore has not established a successful system-level lock. Do not interpret the later `PS Button pressed, forwarding to host` log as proof that the button reached the host or that native Shell handling was suppressed.
- The current `CMakeLists.txt` sets SELF auth ID `0x2F00000000000101`. Upstream `xyzz/vita-moonlight` documents its PS capture requirement as removing the safe `-s` flag and adding SceShell permission/auth ID `0x2800000000000001`. This makes SELF authorization/packaging the leading hypothesis for the lock failure, but the exact meaning of `0x80101801` and the correct authorization combination for this PAF/system-UI app are **not yet verified**.
- **Experimental packaging change now committed on `feat/quick-actions-lifecycle-review`:** `CMakeLists.txt` uses upstream's `-a 0x2800000000000001`, keeps `UNSAFE` (no `-s`), and adds `ATTRIBUTE 0x0E`, matching upstream `xyzz/vita-moonlight` PS-capture packaging. This is a hypothesis-driven test change, not a verified compatibility fix: it may affect the PAF/system-UI app's runtime permissions. Build the VPK, inspect the resulting SELF metadata, and test launching the PAF UI before concluding this combination is valid.
- Stream startup and initial video decode occurred in this excerpt. It contains no evidence sufficient to diagnose the separate video packet-loss reports.

Next step: build the branch and inspect the generated SELF auth ID/attributes; on real Vita, first verify the PAF UI still launches, then capture the exact `sceShellUtilLock` result. Test PS capture enabled (single press forwarded, double press stops stream) and disabled (native PS behavior; no stream stop). Also exercise Quick Actions open/return and confirm controller input is suspended only while the system overlay is active. No build or device test has yet been run for this packaging change.
