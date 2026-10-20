# Development Roadmap

This roadmap captures the agreed next steps for Vita Moonlight PAF. It is ordered by risk reduction and release readiness, not by a promise that every item must be completed before development continues. Keep individual changes small and verify device-specific behavior on real PS Vita hardware.

## Requested: build both application profiles in one invocation

`build.sh` now defaults to sequential normal/system builds in separate
`build/normal/` and `build/system/` trees, sharing one compiled RCO. It publishes
profile-suffixed VPK/SELF/debug ELF files after both builds succeed. Explicit
system-mode ON/OFF arguments retain single-profile builds in `build/` for existing
deployment/manual workflows. Development deployment defaults explicitly to OFF.
Tracing defaults OFF per invocation and can be enabled only for the system half
of a dual build. Both packages retain the same title ID and replace each other.

Shell syntax and isolated helper tests with stub configure/build commands passed:
profile selection, argument quoting, separate trees, resource/artifact copies,
tracing selection, legacy deployment override and failure propagation. Neither
build entry point nor real compiler/configure/packaging was run by the agent.
Maintainer validation of the dual build and both resulting VPKs remains pending.

## Requested: launch alongside an existing Vita application (2026-10-19)

Source research compared BetterHomebrewBrowser's system-mode packaging with Moonlight.
See [docs/SYSTEM_APP_MODE_RESEARCH.md](docs/SYSTEM_APP_MODE_RESEARCH.md). VitaSDK supports
explicit SELF budgets, but Moonlight's ordinary profile requests a default 128 MiB newlib heap;
streaming additionally needs physical-contiguous decoder memory and CDRAM. Changing
only the SFO category cannot establish coexistence.

Source prototype now lives on `codex/system-app-mode`: an opt-in CMake/build-script
profile supplies `gdc`, a 32 MiB SELF budget, boot attribute `0x08`, bounded newlib/PAF
heaps and BHBB-style PAF application/pool settings. Physical reservation defaults to
zero for the frontend-first trial and is configurable after measurement. Sparse
`[Memory]` diagnostics follow Debug log; default launch settings remain available.
Non-build checks cover isolated CMake script branches and invalid budgets, source
grammar/SDK declarations, shell syntax and diff whitespace. No application/resource
compilation, project configure, packaging or device test was run. Acceptance items
below remain unchecked.

Follow-up device report: the app still requested closing the Vita game and emitted
no `[Memory]` lines. The local package from commit `150c538` was inspected read-only:
cache OFF, SFO `gd`, SELF total/physical/attribute zero, and no `[Memory]` strings in
the ELF. This establishes an ordinary local build, not a failed system-mode test or
proof of which package was installed. The user confirmed building with `build.sh`
without arguments, which leaves a fresh/default profile OFF. Separately,
`dev-build-run.sh` previously discarded CMake
arguments and removed the cache; it now forwards arguments and leaves system-mode
VPKs for full installation rather than issuing SELF-only load. CMake and runtime
`[Launch]` messages identify the selected/compiled profile. Correct-profile device
verification is still pending.

The next user run failed immediately on launch with a Vita game resident: an 8 MiB
request (`0x00800000`) in `ddrmain: VLMP00001` failed and a coredump was generated.
The current local VPK now contains the intended system metadata (`gdc`, boot
attribute 8, total 32768 KiB, physical 0) and the memory diagnostics. Source defaults
had copied BHBB's 8/4 MiB surface/text pools despite Moonlight's additional 6 MiB
newlib heap. These pools are now reduced to 4/1 MiB, saving 7 MiB of requested pool
capacity. The failed allocation's caller is not identified by the log; this is a
targeted footprint experiment, not a proven crash fix. Total/physical/heap budgets
are unchanged. Source/whitespace checks passed; build and device retest are pending.

User retest of `7e20e940a52b`: startup no longer crashes and Main/Settings work. The
runtime confirms system profile, AppMgr mode 2 (A), total USER_RW 32768 KiB, free
1632 KiB before Settings, 1428 KiB after showing it, and 1632 KiB after closing it.
PHYCONT is 0/0; CDRAM free/total is 0/16384 KiB; extra memory is 0/0 despite
`allowed=1`. Newlib's arena is only 12288 bytes, with 4760 used, inside its reserved
6 MiB heap; arena statistics do not measure the full reservation. The current AVC
renderer needs PHYCONT plus separate CDRAM framebuffers, so these snapshots do not
establish streaming support. No further allocation settings were changed from
this browsing-only result. The user also explicitly confirms no game-close prompt
and successful return to the same Vita game session. This verifies frontend coexistence
on that device/game; streaming and broader compatibility remain open. These are
user-run device results; no agent build/device test ran.

The user's subsequent stream attempt on `7e20e94` completed launch and RTSP but
failed at stage 8 (control stream establishment), before the Vita video setup
callback. Kernel allocation/thread creation failed; pthread returned 11. USER_RW
free/total was 1328/32768 KiB before setup and 292/32768 KiB at the failure snapshot.
Common-c requests a 1 MiB stack for each transport thread, and control startup
creates multiple such threads. The log does not identify the failing thread or
establish a leak from this early post-unwind sample.

The system profile now applies a project-owned pthread adapter only to common-c
`Platform.c`: 128 KiB stacks, checked setup, attribute cleanup and named `[Thread]`
creation logs. Normal-profile thread sizes, dependency revisions, protocol flow and
SELF/heap budgets are unchanged. Preprocessor-fixture, interpreted adapter failure-path,
isolated CMake selection and whitespace checks passed. No agent compilation or device
test ran. Device retest must establish stack sufficiency and whether stage 8 advances;
the separate mode-A/PHYCONT/CDRAM streaming blockers remain open.

The next user capture confirms the adapter is active: AudioPing plus ControlRecv,
LossStats, ReqIdrFrame and CtrlAsyncCb all report 131072-byte stacks and successful
creation; stage 8 completes. Stage 9 then reaches the video setup callback and
`sceVideodecInitLibrary()` fails with `0x80620840`. Mode remains A with zero PHYCONT,
zero free CDRAM and 648 KiB USER_RW free. Sony's local SDK error table identifies
`0x80620840` as `SCE_VIDEODEC_ERROR_NOT_GAME_BUDGET`: the standard API rejects this
application budget. VitaSDK's NID database/stubs expose a `Nongameapp` family, but
the available headers do not establish its ABI. Verify those signatures and memory
requirements before calling it. The thread-allocation blocker is passed in this test;
long-session stack sufficiency and video/system-mode constraints remain unresolved.

Source cleanup now releases the SPS parser and init-info on library-init failure,
along with the already-freed decoder input buffer. The parser destructor also frees
its owned slice-data object/buffer. Interpreted cleanup-path checks passed; build,
repeated failed setup and allocation-baseline checks on Vita remain pending.

On 2026-10-20 the user supplied a decrypted `SceAvcodecUser` ELF. Export/import
inspection confirms the Nongameapp init/query/create entry points, but their bodies
only forward unchanged arguments to `SceAvcodec`. They do not expose parameter
layouts or the budget check. The next required input is the decrypted SceAvcodec
implementation (`avcodec.skprx.elf`, embedded in the bootimage), or a decrypted
`os0:kd/bootimage.skprx` ELF for extraction. See the research document for file hash,
addresses and import NIDs. No guessed ABI, renderer change, build or device test
was introduced from this wrapper-only evidence.

The supplied bootimage now provides the SceAvcodec and CodecEngineWrapper
implementations. Disassembly establishes Nongameapp query/init/create structure
sizes and shows that automatic allocations still reject a non-game budget.
The next prototype must use explicit library/decoder contexts through CodecEngine,
preserve its unmap/remap ownership order, and provide output buffers outside the
unavailable CDRAM budget. ABI/allocation evidence is recorded in the research document;
runtime sizes, allocation permissions and full streaming remain to be tested.

The system renderer now has an experimental explicit-context adapter. Nongameapp
queries determine storage sizes before allocation; a contiguous uncached main-RAM
arena is opened through CodecEngine, then supplies aligned library and decoder
contexts. Output buffers also use contiguous uncached main RAM instead of CDRAM.
The ordinary renderer path and default 32 MiB browsing budget remain unchanged.
For the next device test use an explicit 65536 KiB total budget with physical
reservation zero; this is headroom for measurement, not a verified requirement.
Cleanup releases decoder, library, suballocations, pool and backing block in order,
retaining backing storage and blocking reuse if runtime cleanup fails. Non-build
failure-path/source-selection checks passed; build and all runtime acceptance
checks below remain pending.

- [x] Prototype an opt-in system-mode package with bounded newlib/PAF allocations and
  budget diagnostics; verify the frontend opens without closing a suspended Vita game.
- [x] Establish decoder/graphics allocations and verify a basic stream with image,
  sound, controls, stop and return to the suspended game on the tested Vita.
- [x] Verify two longer streams, one stop/reconnect cycle and stable sampled
  post-cleanup memory on the tested Vita with the 64 MiB / 6 MiB configuration.
- [x] Verify streaming/reconnect, Settings, Quick Actions and return to the
  suspended game with heap tracing OFF on the tested Vita and 64 MiB budget.
- [x] Smoke-test ordinary-profile launch, Settings, streaming and reconnect after
  the shared pthread-bootstrap fix, with heap tracing OFF.
- [ ] Broaden dialog/error-path, device/game and endurance coverage.

User validation of `8b5d9a5` with total 65536 KiB, PHYCONT zero, mode A:
library query 5767168 bytes, decoder query 8388608 bytes, arena 15728640 bytes;
explicit init/create and all three main-RAM output mappings succeeded. Setup
completed all eleven stages, decoded 1088 frames, then the user stopped with a PS
double press. Audio drain/release and all deferred GXM unmaps returned zero.
The user explicitly confirms working image, sound, controls and resuming the
original Vita game. This verifies the requested basic coexistence scenario on
that device/game, not long-session stability or every lifecycle path.

The same capture contains 120 IDR-wait log messages after out-of-order video at
frame 249; the existing drop-limit request is followed by resumed decoding.
That is recovery evidence, not a new decoder-budget failure or proof of a network
root cause. Final USER_RW free is 31552 KiB versus 33584 before stream setup;
newlib used grows from 93056 to 3279376 bytes. A repeated settled baseline is
needed before classifying that retained footprint. No agent build/device test ran.

The project log callback now limits the two exact common-c IDR/RFI wait messages
to one per kind per second, aggregates omitted repeats and summarizes totals after
transport teardown (including failed setup). It preserves other messages and all
protocol/recovery behavior; no dependency source was changed. State resets before
each session. Interpreted timing/counter checks passed; this logging-only follow-up
has not been built or hardware-tested by the agent.

The longer user run decoded 2732 frames, then common-c reported `Video Receive:
malloc() failed` and requested cleanup. Final newlib arena/used/free were
6287360/5591728/695632 bytes; USER_RW free after deferred frame release was
31412 KiB out of 65536 KiB. This leaves longer-session stability unresolved.
The heap is still capped at 6144 KiB; free main RAM does not expand that cap.
No retained allocation owner is identified by these aggregate counters alone.

An opt-in `MOONLIGHT_HEAP_TRACE` diagnostic now wraps malloc/calloc/realloc/free,
using fixed BSS metadata and sparse call-site snapshots around setup, video receive
allocation failure and stop. It defaults off and reports tracking coverage limits;
it does not change the heap capacity or claim a leak fix. Interpreted actual-source
allocation/realloc/failure/saturation checks and isolated CMake selection checks
passed. Build, runtime overhead and identifying the retained sites require the
maintainer's next diagnostic run and its matching ELF.

The first diagnostic build failed at link. Read-only inspection of its `link.txt`
shows all four `--wrap` flags missing, while the compiled tracer references the
corresponding `__real_*` symbols. A later CMake `LINK_FLAGS` assignment overwrote
the earlier diagnostic flags. It now appends the Vita startup flags, preserving
wrapping. Script-mode checks cover both blocks in source order; successful relink
and the diagnostic device run remain pending. No agent build was run.

The user's `df0792a` diagnostic build links and streams, then hits a 1088-byte
video-receive allocation failure. ASLR-adjusted ELF addresses identify 4048 live
160-byte records from pthread's `pte_new`; trace saturation means the 647680
tracked bytes are a lower bound. The supplied dump ends in ENet's OOM `abort`,
after the attempted AsyncTerm thread also failed to allocate. The entry point
manually initialized newlib but skipped the `.init_array` pthread constructor.
It now calls `pthread_init` before the app starts, verifies stable `pthread_self`
identity and terminates pthread before releasing newlib. This applies to both
profiles without changing heap budgets or dependency sources. Actual-bootstrap
AST checks cover startup/teardown ordering and failure paths. Build and longer/
repeated streams with settled heap measurements remain pending; no agent build ran.

The user now confirms streaming and reconnect no longer crash. The new capture
contains two sessions with 22270 and 18008 decoded frames, both explicitly stopped
by a PS double press with worker result zero. Tracing has zero allocation failures,
dropped records and replaced pointers. PTE records reach 15 / 2400 bytes and stay
at that count after both stops, instead of accumulating thousands. After deferred
frame release, newlib used is 96768 then 96936 bytes (+168), and USER_RW free is
32952 KiB in both cases. The tested failure and one reconnect cycle are verified;
this does not establish zero retained bytes over arbitrary reconnect counts.
The first session also logs overlay enter/return, neutral host input and PS
suppression clearing; broader native-UI verification remains separate. These are
maintainer-run device results; the agent only parsed logs and inspected the ELF.

The user subsequently reports all requested checks passed with heap tracing OFF:
streaming, reconnect, Settings, Quick Actions and returning to the resident Vita
game, using the same 65536/0/6144 KiB total/physical/newlib configuration. This is
an explicit user result, not a new measured memory log. The ordinary profile still
needs its regression smoke test because pthread bootstrap is shared. No agent
build/device test ran; this follow-up changes only verification documentation.

The ordinary-profile regression test now passes by explicit user confirmation:
launch, Settings, streaming and reconnect with system mode OFF and heap tracing
OFF. Both profiles have passed their requested smoke tests on the user's Vita.
The fresh system-profile total-budget default is now the tested 65536 KiB;
physical/newlib/PAF remain 0/6144/10240 KiB. System mode and heap tracing default
OFF, and ordinary SELF settings remain unchanged. Existing CMake cache overrides
are preserved. Isolated script-mode checks cover default/override selection and
both SELF branches without configuring or building the project.

## Requested: full stream diagnostics overlay (2026-10-19)

Replace the existing FPS-only display with useful stream diagnostics based on the
user's Moonlight Qt reference. The source-based metric inventory, measurement limits
and implementation sequence are in [docs/STREAM_DIAGNOSTICS_PLAN.md](docs/STREAM_DIAGNOSTICS_PLAN.md).
Source implementation is now on `codex/stream-diagnostics`. The FPS-only UI is replaced by a
compact overlay of at most six lines, refreshed independently of video arrival. The existing
`show_fps` key/default is preserved under the new label `Display stream statistics`. Video/audio
hooks collect session-tagged counters and rolling timings; RTT is sampled by the existing session
owner before transport cleanup, so UI snapshots do not wait for ENet. Unsupported video RTP/FEC
fields are omitted. Decoder time is explicitly API-call time; PAF submit timing is CPU time, not
display/GPU completion. Core source and resource checks are complete; all build/device acceptance
items below remain open, including numerical and performance verification.

User follow-up (2026-10-19): the original 18-line overlay was visible on Vita but covered too
much of the image. The supplied stalled-stream screenshot shows no incoming/decoded/PAF frames
while RTT remains available, with no decoder errors. The layout is now reduced to a 660 x 136
panel; unavailable rows and audio/CPU/stride/window/cumulative-counter details are omitted.
This is a source change awaiting another device check, not verification of stream stability.

Source review of that failure found repeated `PresentTask` registration with no corresponding
unregister. PAF call-list entries are persistent (also demonstrated by NetStream's surface-update
lifecycle), so registrations could grow throughout streaming. Stream now registers one presentation
callback for its lifetime; the deferred framebuffer release callback also unregisters explicitly.
This removes an unbounded registration pattern; its contribution to the reported hang still needs
device confirmation.

The supplied log also proves a separate exit failure: double PS reaches `moonlight_stream_stop()`,
but `pthread_create` for the stop worker repeatedly returns 11 (`EAGAIN` in the installed SDK).
That indicates unavailable thread resources; it does not identify which resource was exhausted.
The launch worker now remains alive for the whole session and owns RTT polling plus teardown.
Stop and termination callbacks only set mutex-protected requests; no new stop/RTT thread or
condition-variable wait is needed. The owner calls `vita_input_stop()` / `LiStopConnection()`
before reporting stopped, retains running/cleanup ownership through API event bookkeeping, and
rejects another start until it finishes. Partial setup failures still use common-c's own unwind.
This also addresses the source ownership problem in API audit F1; runtime acceptance stays open.

Non-build checks: XML/style checks for the compact overlay, source registration-count checks,
session C-subset grammar parsing (not SDK type checking), and interpreted checks of the actual
state-handler bodies for idle stop, pending cancellation, repeated stop, remote error and final
completion. No application/resource compilation or device test was run. Re-test a long session,
double PS after a stall, host/network loss, pending launch cancellation, Quick Actions and repeated
stop/reconnect with statistics both enabled and disabled. The original freeze is not yet reproduced
or conclusively attributed to one cause.

- [ ] Implement session-scoped video statistics and safe snapshots: incoming/decoded/PAF-submit
  FPS, host processing latency, estimated missing frames, local drops, ENet RTT/variation,
  decode/queue/CPU-submit timing and video payload bitrate. Label PAF/RTT estimates accurately.
- [ ] Replace the FPS text with a readable CXML overlay, retaining the `show_fps` AppSettings key;
  refresh without per-frame string formatting and age stale values even if video stalls.
- [ ] Validate the implemented Opus parameters, decode/output call timing and error counters
  alongside core video; add synchronized RTP audio/FEC snapshots and missing RTP video counters
  only as separately reviewed follow-ups, without displaying unsupported zeros.
- [ ] Maintainer builds and verifies readability, numerical behavior, performance, network loss,
  Quick Actions, stop/reconnect and teardown on Vita; record results before marking complete.

## Stream system infobar (2026-10-19)

Source investigation on `codex/research-stream-infobar` identified NetStream's explicit
`sceAppMgrSetInfobarState()` calls for playback entry/exit and controls visibility. See
[docs/STREAM_INFOBAR_RESEARCH.md](docs/STREAM_INFOBAR_RESEARCH.md) for pinned reference code,
Moonlight lifecycle integration points, orderly-shutdown restoration and the device checks.
Source implementation now uses a small AppMgr adapter in `MoonlightApp`: a valid Stream page
requests hidden and its destructor requests visible. Orderly shutdown also restores visibility
before disabling event delivery. Failed restores remain pending; transition results are logged
through `vita_debug_log()`. No per-frame visibility enforcement or input changes were added.
User confirmed that everything works on 2026-10-19 after implementation commit `85899c5`.
This records user-reported Vita verification of the requested infobar behavior, not an agent-run
build/test or a detailed result for every lifecycle/failure case below.

- [x] User verified the implemented infobar hiding/restoration during streaming on the tested Vita
  (2026-10-19).
- [ ] Verify start/stop/reconnect, launch failure, host loss, Quick Actions/return, native keyboard,
  application exit and the restored browsing-bar appearance on Vita.

## Requested: remote self-unpair when deleting a PC (2026-10-19)

User requests that deleting a saved PC also revokes this Vita's pairing in Sunshine, and explicitly
declines adding an administrative login to the Vita. This supersedes the earlier decision to treat
remote revocation itself as intentionally unsupported; administrative access remains out of scope.

**Status: requested, not implemented; unavailable through the reviewed stock Sunshine client API.**
Rechecked Sunshine master `3d6e1d6008bd4a24906283ab25fcd0becc0a1c07` on 2026-10-19:
GameStream registers serverinfo/pair/applist/appasset/launch/resume/cancel, with no unpair/self-revoke
route. `POST /api/clients/unpair` is an admin Web API operation requiring Basic username/password and
the server-generated client UUID; a pairing certificate does not authorize it. The admin UUID is
not the local uniqueid or host UUID. See audit A21 for pinned handler/route references.

- [ ] Obtain a supported Sunshine client self-revocation operation authenticated as this paired
  Vita, or an explicitly agreed host-side extension. No admin-login feature is to be added.
- [ ] Once that protocol prerequisite exists, implement remote revocation through HostService →
  backend/adapter, targeting only this Vita on the selected host, before reporting successful remote
  unpair/local removal. Define offline/rejection/retry and explicit local-only removal behavior.
  Preserve the installation-wide identity and pairings on other hosts.
- [ ] Verify the selected server-side record is gone, other clients/hosts remain paired, and rejected,
  offline and repeated operations never report false unpair success.

Until then Delete remains local metadata removal. Do not send a nonexistent GameStream `/unpair`,
substitute `/cancel` (which quits the host app), or delete/rotate global client credentials to imitate
remote revocation. No runtime unpair support or Sunshine-host test was added for this investigation.

## Pairing completion and Back navigation (2026-10-19)

User reproduced Apps → Back returning to the completed PIN page instead of Search PCs.
Root cause: successful Pairing pushed Apps without removing itself from Base's page stack.
Search also retained `m_host_selected`, preventing another selection after a child page returned.

The source change removes Pairing only after Apps opens successfully, then restores Apps' event
callback after Pairing's destructor hands it back to its parent. Search callback restoration clears
the old selection guard/index and restores the list prompt. Apps-open failure retains Pairing with
its error. This uses the existing Base stack/removal/reactivation and main-thread event delivery;
BetterHomebrewBrowser's page stack is the reference for ownership, not a second navigation system.

- [x] Verify on Vita: Search → unpaired PC → successful Pairing → Apps → Back returns directly to
  Search; select a PC again. User confirmed this exact requested flow works on 2026-10-19.
  This is user-reported verification on the tested Vita, not an agent-run or cross-firmware test.
- [ ] Check already-paired Search → Apps → Back, Pairing cancel/failure, and Main/manual → Pairing →
  Apps → Back returning to Main. Check Apps still receives its stream events after Pairing destruction.
  The user's confirmation of the pairing/Back/reselection flow does not verify these additional cases.

Validation: `bash build.sh` completed CXML compilation, C++ build, SELF and VPK generation with
the existing build directory. The first attempt stopped during PAF-header discovery; read-only
inspection found the SDK headers and the second unchanged build command succeeded. Existing SDK
signedness/wchar_t and future-resource-timestamp/clock-skew warnings remain. VPK CRC validation passed
and its packaged eboot matches the newly built SELF. `git diff --check` passed. No deployment or Vita
navigation test was performed by the agent.

## Main cold start: one saved PC verified by user (2026-10-19)

User supplied the Vita log:

```text
[Main] RefreshHosts hosts=1 cells before=1 after_delete=0 after_insert=1 selection=0
```

In response to the explicit cold-start question, the user confirmed that the PC appears
immediately without connecting or opening Settings first. This verifies the visible one-host
cold-start scenario on the tested device/build. The log shows one backend host, complete cell
deletion and one inserted cell; no duplicate is indicated by these counts.

The installed package revision was not supplied with this result, and the
`[Main] initial refresh finished ...` line is still absent, so warm-up completion/stability
cannot be inferred. Empty-storage and multiple-host cold starts remain unverified. Keep the
full Main acceptance item open; no further InitialRefreshTask change is justified by this result.
No build or hardware test was run by the agent for this documentation update.

User explicitly reports that the initial-refresh completion line is absent. Source follow-up:
the line remains in `Main::InitialRefreshTask`, but `src/common.cpp` initializes
`s_debug_log_enabled` to zero on every process start. The persisted Debug log setting is applied
by `moonlight_settings_apply_runtime` after lazy AppSettings initialization; the normal Main
startup path does not initialize it. Initial-refresh diagnostics can therefore be suppressed
even when Debug log was enabled in the previous process. A later RefreshHosts log can come from
another refresh after settings become active; it does not establish when startup refresh finished.
This explains a diagnostic visibility gap in current source, not a reproduced refresh failure.
The earlier enable-Debug-log-and-restart instruction is insufficient to guarantee startup capture.
Do not require the user to repeat this successful one-host test just to obtain that line, and do
not change InitialRefreshTask or force logging on the basis of its absence. No runtime change was made.

## Settings-open regression: verified resolved on tested Vita (2026-10-19)

**Status:** user verified native Settings open/close and persistence across a complete
application restart on 3.60 HENkaku without ioPlus with authority `0x2200000000000001`.
PS double-press stream stop/cleanup, short PS forwarding, hold/Quick Actions and
reconnect are also user-confirmed on that build. No cross-firmware claim is made.

**Diagnostic cleanup:** temporary Settings/FIOS probes, screen-report logic,
CMake diagnostic options and forced logging were removed after verification.
Raw PS callback/edge/timing traces were removed; functional input handling,
state/error logs, the authority fix and required Main refresh logs remain.
Investigation steps and artifact names below are historical.

**Original priority: diagnose before the API audit's F1 implementation follow-up.** The user reports that
saved PCs now appear on Main at application startup, but attempting to open Settings freezes the
application and eventually crashes with `C2-12828-1`. The user subsequently confirmed the crash
occurs **after selecting Settings**, and supplied a textual crash report followed by the complete
`.psp2dmp`. The exact installed package revision and useful loader logs remain unconfirmed.

- The startup-list observation is user-reported device evidence for that run. It does not complete
  the empty/one/multiple-host cold-start matrix or prove the host/cell diagnostics match.
- Source review traced OptionMenu → Main suspension → SettingsService → LegacyMoonlightAdapter →
  lazy AppSettings initialization → Interface::Show. No specific cause has been established from
  the error code and symptom alone. No runtime behavior has been changed for this report.
- Local NetStream and BetterHomebrewBrowser references also pass an InterfaceCallbacks table
  allocated on the stack to Show; stack allocation alone is not evidence of this crash's cause.
- Crash evidence: prefetch abort, PC/IFAR `0x0`, LR `ScePaf RX + 0x58919`, another reported PAF
  frame at `+0x15fe31`. Reported peak stack use is 2168 of 10192 bytes; this excerpt does not show
  a stack overflow. R2/R12 are annotated as application RX `+0x21c010`.
- `arm-vita-eabi-nm` on the local baseline ELF maps exactly RX `+0x21c010` to the import stub for
  `sce::AppSettings::PluginSetParamCB` (library NID `0x9522D063`, function NID `0xB3935DD3`).
  This is consistent with a failure during PAF plugin initialization, but a register containing
  that stub address does not prove which instruction branched to zero or why. The installed
  package must match the ELF for definitive symbolization; no matching firmware PAF code was
  available to disassemble the LR site. Do not claim an unresolved import or callback-lifetime
  defect as established from this register match alone.
- The agent ran `bash build.sh` successfully with the existing build directory: CXML, application,
  SELF and VPK targets completed. This was not a clean build, deployment or Vita runtime verification.
- Full-dump analysis (2026-10-19): captured PAF export/unwind tables recover
  `Plugin::Plugin → internal PAF frame → Plugin::LoadSync → application`.
  The application return maps to the existing `moonlight_settings_init` LoadSync call in the
  comparison ELF. This localizes the failing stage before XML initialization / Interface::Show.
  The 18 registered-module records include BXCE, INI processor and Common GUI Dialog, but no
  AppSettings module. Loader failure is a supported investigation direction, not a proven cause.
  The PAF call-site and runtime import-stub instructions are not captured. Details and evidence
  limits are in [docs/SETTINGS_CRASH_ANALYSIS.md](docs/SETTINGS_CRASH_ANALYSIS.md).
- Device follow-up (2026-10-19): user confirms **3.60 HENkaku** and that restoring **ioPlus**
  resolves the problem; NetStream/BHBB reportedly open Settings without it. This supports an I/O
  environment dependency, not a proven category/authority-ID cause. The ux0/uma0 path selection
  still requires separate verification and actual filesystem error codes if it remains wrong.
- [x] Run the isolated `CATEGORY=gd → gdc` packaging experiment without ioPlus. **Negative device
  result (2026-10-19): user reinstalled, disabled ioPlus and reproduced the problem.** Category
  alone is not a demonstrated fix. NetStream and
  BHBB use gdc; our default was gd. Only `VITA_MKSFOEX_FLAGS` changes; authority ID, SELF flags,
  runtime and storage remain unchanged. `bash build.sh` passed; VPK comparison confirmed that
  only the SFO CATEGORY value differs and eboot/ELF/resources are byte-identical to the gd control.
  Artifacts are under `build/category-test/{gd,gdc}/`, with a hash manifest. Full procedure and
  results are in [docs/SETTINGS_CRASH_ANALYSIS.md](docs/SETTINGS_CRASH_ANALYSIS.md).
- Category has been restored explicitly to `gd` so inherited CMake cache flags cannot keep gdc.
  Added default-off `MOONLIGHT_SETTINGS_DIAGNOSTICS`: startup mkdir results, read-only access
  probes for the native AppSettings module/resource, dependency load returns and SetParam import
  words all go through `vita_debug_log` with prefix `[SettingsDiag]`. Only that prefix bypasses
  the normal log toggle in this explicitly enabled diagnostic build; ordinary logging stays off
  by default. No streaming/file logger or alternate module loader was added.
- [x] Capture filesystem `[SettingsDiag]` with ioPlus disabled and enabled (user logs below).
  Original procedure: capture from `build/settings-diagnostics/` with ioPlus
  disabled, then enabled, on otherwise unchanged 3.60 HENkaku. Interpret filesystem return codes
  before changing authority IDs or loader behavior. Accessible files do not prove module start
  succeeded. The raw loader result is still not intercepted inside PAF.
  Both normal and opt-in diagnostic builds passed locally; device behavior remains pending.
  No deployment or plugin change was performed by the agent.
  Do not change InitialRefreshTask, SELF authorization, storage formats or callback lifetime
  speculatively. SAFE/AppSettings module research remains closed.
- Console follow-up (2026-10-19): user sees CommonDialog Init/Term messages but no SettingsDiag
  output. Connectivity exists; user-process stdout capture or the exact installed variant is
  still unconfirmed. The local diagnostic VPK contains the expected marker. Do not interpret
  the absence of console lines as a successful file probe or proof of a new root cause.
- [ ] Use opt-in `MOONLIGHT_SETTINGS_DIAGNOSTICS_SCREEN` to show access/dependency results in a
  native message dialog when Settings is selected, before any AppSettings load. Separate artifact:
  `build/vita_moonlight_paf-settings-screen.vpk`. Screen, console-only and default builds pass
  locally; SFO/SELF values and mutually exclusive diagnostic markers were checked. This report
  build deliberately disables actual AppSettings initialization and is not a production fix.
  Next: obtain a photo/transcription of the report without ioPlus, then with it, on the same VPK.
- ioPlus-enabled diagnostic run (2026-10-19): user now receives the probe lines. Both native
  files have stat=0, successful open and read=4 (SCE/RCOF headers); all three dependency loads
  return zero. This confirms execution of diagnostic code and access in the enabled case only.
  The report dialog accepts Init with zero, then prints `SceMsgDialog Parameter Error : 9` and
  terminates. This is a separate diagnostic-UI failure before AppSettings is attempted.
  Its exact internal validation rule remains unknown. The 291-byte, nine-line report has been
  reduced to a three-line summary; dialog GetResult is now logged on completion. Screen build
  and package validation pass, but the text-limit hypothesis and actual display need device
  verification. The subsequent ioPlus-disabled capture below completes the file-access comparison.


- ioPlus-disabled capture (2026-10-19): both ux0 mkdir attempts return `0x80010013`
  (ENODEV, **not** EACCES); uma0 returns `0x80010011` (existing directory) and is
  selected. Both native Settings files return ENODEV from stat/open; no read occurs.
  BXCE/INI/Common GUI still load successfully. Thus filesystem access explains the
  fallback to uma0 and supplies a concrete prerequisite failure for AppSettings.
  It does not yet identify the firmware policy or prove which PAF instruction calls zero.
- [x] Capture the new read-only FIOS `overlays`, `overlay` and `resolve` lines with ioPlus
  disabled. Diagnostics enumerate only this process and resolve the four probed paths;
  they never add/remove/modify overlays or disable thread filtering. The installed SDK's
  missing 02 declarations are supplied as a local ABI subset from current vita-headers.
  Production builds do not import these diagnostic calls. Test package:
  repository-root `vita_moonlight_paf-settings-screen.vpk`; it still bypasses AppSettings.
  If a failed path resolves to `invalid:`, use the matching rule/order to investigate its
  creation and authorization policy before proposing a minimal fix.

- Hardware FIOS result: user capture on fb4fd6a confirms 10 order-0x80 rules
  redirect physical device roots including ux0/vs0 to `invalid:/`; ResolveSync02
  returns those invalid paths. The uma0 path remains unchanged. This establishes
  the access-failure mechanism; the rule-creation policy is still unverified.
- [x] Run isolated authority-ID comparison with
  `vita_moonlight_paf-settings-auth220.vpk` (same diagnostic runtime, BHBB's
  `0x2200000000000001` authority). Local comparison proves only two authority
  bytes in SELF changed; only eboot differs in VPK. Normal build is unchanged.
  Compare rules/resolve/file logs without ioPlus. Real AppSettings/persistence,
  streaming, PS/Quick Actions checks remain required before adopting any ID.
  See SETTINGS_CRASH_ANALYSIS for hashes and evidence limits.

- Authority comparison device result: with only SELF authority changed to
  `0x2200000000000001`, user logs show four 0x81 mappings, no 0x80 blocking rules,
  successful ux0 selection and readable SCE/RCOF headers for both Settings files.
  This confirms authority as a causal variable for filesystem access on this 3.60
  device. It does not yet verify actual AppSettings initialization or persistence.
- Build now uses the BHBB authority ID above, retaining gd/UNSAFE/SELF attributes
  and all runtime/storage code. The previous ID's persistence/PS observations were
  insufficient to establish ioPlus-free access. Normal and console-only diagnostic
  builds must be tested through real Settings; SCREEN mode is not used for this step.
- [x] Verify `vita_moonlight_paf-settings-auth220-live.vpk` without ioPlus: user confirms
  Settings opens/closes and changed values persist after complete application restart.
  Two supplied launches reach `native plugin LoadSync returned found=1`.
- [x] Verify double-PS stream stop on the new authority without ioPlus: user confirms
  the supplied stop/cleanup log is from this variant. Worker completion, successful
  audio drain/release and deferred video-buffer release are recorded.
- [x] Regress reconnect and deliberate PS tap/hold/Quick Actions under the new authority:
  user confirms all work on the same new VPK without ioPlus.
- [ ] Verify new saved-host writes under ux0 persist. Existing uma0 data is not migrated.

- Real-loader follow-up: supplied log shows two separate processes of the expected
  live build (`72819b499d4a`) reaching `native plugin LoadSync returned found=1`;
  SetParam import is patched after loading. Both select ux0, have no blocking
  overlays and refresh one host with after_delete=0 / after_insert=1. The failed
  plugin-load stage is passed on hardware. User subsequently confirmed visible
  Settings operation and persistence, closing the reported regression.
- The accompanying stream-stop excerpt reaches worker completion and deferred
  video-buffer release; user confirmed its provenance as the new authority VPK
  without ioPlus. Subsequent confirmation also covers Quick Actions/PS/reconnect.


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
- [ ] Reproduce and fix the Main screen's saved/paired-PC list not loading on entry: the list is empty until the user connects to a PC, opens System Settings, and returns to Main. Trace initial host-store loading, page lifecycle/refresh dispatch, and paired-host filtering; verify cold start and returning to Main without first connecting.
  - Source review found that `InitialRefreshTask` previously stopped as soon as the backend reported any hosts, without checking whether the PAF `ListView` had created the matching cells. The warm-up now waits for a minimum delay and three consecutive host/cell-count matches (with a bounded timeout). User verified immediate visibility with one saved PC on 2026-10-19; the supplied refresh log has `hosts=1 after_delete=0 after_insert=1`. Empty/multiple-host cold starts and the initial-refresh completion log remain outstanding; the full acceptance item is not complete.
- [ ] Check the PS4 Link style Search PCs and Pairing pages on hardware against the PS4 Link screens: header position and the fading line, the pairing page's flow bar and Back button position, the saved-PC row title and description positions, and that touching the row text still activates the row.
- [ ] Add a PC manually, including a custom port; verify unsuccessful attempts do not create an incorrect saved-host record.
- [ ] Pair successfully and verify incorrect-PIN errors are distinguished from transport/protocol failures.
- [ ] Retrieve the app list, start a stream, and check video, audio, controller input, and touchscreen input.
- [ ] Stop the stream, return to the app UI, and connect again; check that resources and callbacks are cleaned up safely.
- [ ] Exercise connection failure, host/network loss, recovery, and relevant PS Button/application lifecycle behavior.
- [ ] Verify gyroscope and accelerometer reporting on hardware; the input path exists but is not yet fully hardware-verified.

Capture the Vita runtime log (`vita_debug_log`) for every step and attach the relevant prefixes to the result: `[Search]` and `[Main]` for the host lists, `[Pairing]`, `[GameStream]`, `[ConnectionService]` and `[LegacyAdapter]` for connect/pair/launch, and `[Stream]`, `[StreamPage]`, `[Video]`, `[Audio]`, `[Input]`, `[Touch]` and `[Motion]` for the stream itself.

**Done when:** the main journey and important failure/recovery paths have been exercised on Vita, observed problems are either fixed or documented with reproduction steps, and stream shutdown/reconnect does not leave the app in a broken state.

Not part of the PS4 Link parity pass because the needed pieces do not exist in this app: the animated sync icon on the search page, the "Searching via Internet" illustration page and its Skip button (discovery is LAN only), the monitor illustration and Register button on the pairing page (the Vita shows the PIN and pairs immediately), and the PS4 Link number field style. The Search PCs page keeps its corner back button instead of PS4 Link's Cancel button in a bar, since it has no second button to justify a bar. The Apps page still uses the generic row and the corner back button.

## 2. Make development builds reproducible

**Priority: next, and can be worked on alongside device validation**

- [ ] Record the exact supported VITASDK revision (it cannot be derived from the repository; it has to come from the maintainer's working environment).
- [ ] Record the exact revisions of `vitasdk-paf-component` and `psp2cxml-tool`.
- [ ] Document dependency setup and environment variables in a clean, repeatable setup procedure.
- [ ] Validate a clean build with `bash build.sh`, including CXML-to-RCO compilation and VPK/SELF generation.
- [ ] Document the expected toolchain and known build warnings; do not treat a build as verified unless it was actually run.
- [x] Generate `build/vita_moonlight_paf.elf` automatically for crash-dump symbolization (2026-10-18). CMake copies the unstripped linked executable and preserves the extensionless input expected by VitaSDK's packaging helpers. Verified with `bash build.sh` (existing build directory), successful SELF/VPK generation, byte equality and `.debug_info`/`.debug_line`/`.symtab` inspection. The direct CMake debug-ELF target also regenerated a missing `.elf` with identical SHA-256. This does not verify the Settings crash fix or GUI symbolization on a matching dump; preserve the ELF from each installed VPK.

**Done when:** a fresh environment can follow the instructions to build the project without undocumented manual fixes.

## Sunshine / GameStream client API coverage

- [x] Complete the source-level inventory of client-facing Sunshine-compatible GameStream APIs/protocols, including purpose, request/response semantics, authentication/pairing, local implementation, upstream references and coverage status. See [docs/SUNSHINE_API_AUDIT.md](docs/SUNSHINE_API_AUDIT.md), audited on 2026-10-18 against local `761d730` and pinned upstream revisions. This checkbox covers source review only, not runtime compatibility.
- [x] Compare actual Moonlight/Vita Moonlight/common-c and Sunshine implementations; distinguish client GameStream APIs from administrative Web API. The audit confirms that `/cancel` is remote Quit, not ordinary disconnect or cancellation of an in-flight launch; the reviewed Sunshine has no GameStream `/quit` or `/unpair` route. Admin integration remains out of scope.
- [ ] Implement missing client-side API support that is required for compatibility, in small changes following the existing backend/legacy-adapter boundaries; preserve protocol behavior from upstream implementations rather than inventing shortcuts.
- [ ] Exercise the relevant endpoints against a real Sunshine host, including success, error, and version/feature-compatibility cases; document any intentionally unsupported endpoints and why.

Source findings and isolated follow-ups (basic user smoke test passed; full runtime acceptance remains pending):

**User confirmation, 2026-10-20:** after the source follow-ups through `22ff3ae`, the maintainer
replied “всё ок” to the requested Vita sequence: repeat PIN pairing → Apps → application restart →
connect without another PIN → launch and stop streaming. Record this as user-reported success
of that normal flow, including reuse of trust after restart. The installed revision, build log,
Sunshine version/configuration and certificate-file inspection were not separately supplied; this
is not an independent TLS-handshake trace or verification of rejection/error cases.

- [x] **Unexpected-termination cleanup ownership (audit F1): established-stream control disconnect → owner cleanup → manual resume verified on one Vita, 2026-10-20.** Source implementation is already present in `0cef1ac`; do not implement the obsolete callback-only fix again. Supplied logs show unexpected ENet disconnect, termination result=-1/cleanup requested, session-owner stop, all common-c stop/cleanup stages, audio drain/release result=0 and three framebuffer unmaps result=0 before another StartApplication. Then resume app=1093255277 is accepted, setup result=0 and video decodes through frame 361. This tests stream teardown and restart using the existing paired host (has_host=1/state=2/initialized=1), not the separate DisconnectHost/ConnectHost API. Repeated failures, setup/termination races, pending launch cancellation, full host shutdown and other network-failure modes remain open. The log also reports no audio traffic for the stopped session; the maintainer subsequently confirmed audible sound on Vita (2026-10-20). This is not a diagnosed audio regression; exact session attribution of the warning remains unproven and no audio code change is warranted. Single-cycle memory samples are not a leak-free guarantee.
- [x] Refresh host active-app state before launch/resume and reject a different running app instead of silently resuming it (F2), verified for the three tested normal decision branches on one Vita. Source implemented 2026-10-20: HTTPS-only pre-start snapshot, pairing check, same-app resume/idle launch, conflict result -8, and cancellation checks. Normal launch/stop and reconnect smoke test reported OK by the user; different-app conflict verified on Vita by user confirmation and logs (2026-10-20): selected=881448767, active=1093255277, setup result=-8. Same-app resume also user/log verified (2026-10-20): resume app=1093255277, accepted rtspenc URL, RTSP handshake complete, connection started, setup result=0 and decoded frames through 901. Host-app exit while keeping Apps open also user/log verified (2026-10-20): fresh state selected launch app=1093255277, accepted rtspenc URL, all setup stages completed, result=0 and decoded video through frame 841. This closes the basic F2 decision regression (different app / same app / exited app); snapshot races, additional host-change cases and the remaining T5/T8 matrix stay open. The launch log includes an IDR wait/drop-limit/request sequence followed by resumed decoding; stream-quality root cause and sustained quality are not established. Do not automatically quit another app.
- [ ] Validate GameStream XML status/session results with Expat, fix app-list parsing and invalidate cache on host transitions (F3). Source implemented 2026-10-20; C grammar checks passed and normal Apps/stream flow reported OK by the user; actual C fixtures, malformed/error XML and full Vita matrix remain pending. Transfers now own callback/buffer/timeout under the HTTP mutex. Keep scalable UI storage as a separate change. The backend limit is 64, but Apps still displays only eight.
- [ ] Complete pairing/certificate validation (F4). On 2026-10-20 the source signature check now accepts only EVP result 1, verifies the signed server secret before classifying a PIN mismatch, and clears stale per-attempt errors. Non-build actual-expression AST checks cover all 27 combinations of -1/0/1 EVP return values and proof-before-PIN ordering. Per-host `server.pem` storage and exact-leaf TLS verification are source implemented with maintainer approval (2026-10-20). HTTPS without a pin is forbidden; old records need PIN pairing, mismatches fail without downgrade, and final-pairing failure rolls back temporary trust. Pairing/shutdown are excluded to protect credential lifetime. Six non-build Python checks pass. User confirmed normal pairing and reconnect without PIN after restart, followed by launch/stop; no separate certificate/TLS trace was supplied. Mismatched valid server.pem rejection and restoration are user/log verified on Vita (2026-10-20): the wrong certificate returns -10 (SSL peer certificate verification failure); after restoring the original, ConnectHost succeeds and applist count=4 total=4 without a new pairing exchange in the supplied log. Host-side revocation followed by PIN pairing and app retrieval is also user/log verified (2026-10-20): a new PIN page appears, all five pairing phases complete, then applist count=4 total=4. The maintainer then confirmed fresh-process reconnect to Apps without another PIN after this specific re-pair (2026-10-20); that final check is user-reported, without a new log. Wrong-PIN rejection is now user/log verified (2026-10-20; maintainer-reported Sunshine 2025.924.154138): legacy=-1, semantic result=-1001 (Incorrect pairing PIN), UI error/new PIN and a fresh getservercert request. The subsequent Vita log now reproduces retry failure: getservercert returns an empty HTTP response, mapped to network error -1002 and page exit. Successful retry is not verified. See accepted noncritical compatibility limitation F6; no Sunshine patch or client workaround is planned. Malformed/unreadable pins, failed persistence, malformed-proof/crypto error paths and remaining T3/T9 cases stay open.
- **Accepted noncritical compatibility limitation (F6, 2026-10-20):** wrong PIN → new PIN → empty getservercert response → -1002/page exit on Sunshine 2025.924.154138. The maintainer confirms the failure occurs after submitting the new PIN. Wrong-PIN rejection works; retry fails in this tested sequence. Per explicit maintainer decision, do not patch Sunshine or add client workarounds for this issue. It does not block independent client API work.
- [ ] Run audit tests T1–T10 with recorded Vita/package and Sunshine version/configuration. API methods with no UI caller require a backend-level test trigger; page close alone does not prove DisconnectHost ran (F5).

Current independent work order: F1–F4 have source follow-ups. The user confirmed the basic pairing/Apps/restart/reconnect/launch/stop flow. Different-app rejection is now user/log verified: app A remained running and selecting B returned -8. Same-app resume is now user/log verified through successful setup and decoded video. Host-app exit without reconnect is also verified to select launch and produce decoded video; the basic F2 decision branches are complete on the tested device. F1 established-stream unexpected-disconnect cleanup and manual resume are now log verified. The maintainer also confirmed audible sound on Vita. TLS rejection of a mismatched saved server certificate and recovery after restoring the original are now user/log verified. Host-side revocation → new PIN → successful pairing and app retrieval is now log verified. Reconnect without PIN after that specific re-pair is now user-confirmed. Wrong-PIN rejection is verified, but the subsequent retry is now known to fail with an empty getservercert reply (-1002). F6 is an accepted noncritical compatibility limitation, with no Sunshine fix or client workaround planned. The next independent implementation task is scalable app-list presentation (64 backend / 8 UI); malformed/error, setup-cancel/race and repeated-recovery cases remain open. Continue the remaining error/trust matrix before claiming compatibility. F5 state ownership and the remaining T1–T10 matrix also stay open. The maintainer runs builds and device tests. Main cold start has user verification for immediate visibility with one saved PC (2026-10-19); empty/multiple-host cases and the initial-refresh completion log remain open. Do not modify `Main::InitialRefreshTask()` speculatively. Older chronological notes retain their original verification status.

**Done when:** the API inventory is checked against source and runtime behavior, required missing client functionality is implemented, and remaining unsupported cases are explicitly documented.

## 3. Close technical risks and improve testability

- [x] Verify the `RefreshHosts` cell-count fix on Vita: user confirmed the host list behaves correctly. Reported logs show `after_delete=0` and `after_insert=hosts` across repeated refreshes, selection-mode toggles, and removing one of two hosts. The user reports the remaining list behavior is OK. A clean build was not separately verified. Source review found that `DeleteCell` was passed `existing - 1` even though BetterHomebrewBrowser passes the full `GetCellNum(0)` count.
- [ ] Verify the same `DeleteCell(0, 0, existing)` call on the Search PCs and Apps lists on Vita. A Search PCs screenshot showed three rows for two found PCs, the third a stale copy of the first, which is the `existing - 1` symptom; after the fix two found PCs must show exactly two rows when the scan finishes, and repeated scans must not add rows.
- [ ] Identify small, hardware-independent logic suitable for tests, starting with host identity resolution, pairing-result classification, and host persistence decisions. Separate each decision from the `sceIo*`/PAF calls around it first, so it can be built and run with a desktop compiler.
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

### PS double-tap forwarding follow-up (2026-10-14)

The latest device report confirms the intended basic behavior when `ps_button_capture` is enabled, but the first PS tap is forwarded before the second tap can stop the stream. The input path previously put `SPECIAL_FLAG` directly into every controller report while the physical PS bit was held, so stopping on the second edge could not retract the first report.

**Change committed:** PS forwarding is now deferred until the 250 ms double-tap window expires. The physical PS state is tracked separately from the controller state sent to the host; a confirmed double tap clears the pending single tap and stops the stream without sending the pending PS event. A confirmed single tap emits a short host-side PS pulse after the window. Commits: [defer forwarding](https://github.com/NowaLone/vita-moonlight-paf/commit/fb0b2612ec22283541267bacf4c28ec6dcc0ea48) and [track physical PS edges separately](https://github.com/NowaLone/vita-moonlight-paf/commit/81acad9146f6e4b3adb3ef64a7ec304e07ba20de).

This change is source-reviewed only; no build or hardware test has been run. Verify that single PS sends exactly one event after roughly 250 ms, double PS stops the stream without sending PS to the host, and a held PS does not generate repeated events.

**Quick Actions while holding PS remains unresolved.** The user reports Quick Actions still opens when capture is enabled and PS is held, while the lock call had previously returned `0x80101801`. Do not claim this is fixed by input buffering: host-side event timing and Shell-level interception are separate mechanisms. Capture the latest `ShellUtil init events` and lock result from the revision containing the initialization call; only pursue Quick Actions suppression after confirming the actual lock result and supported API behavior. When capture is disabled, preserve native Shell behavior and do not acquire the PS lock.

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


### Quick Actions hold behavior clarification (2026-10-14)

The intended behavior is to **allow the system Quick Actions UI to open** when PS is held, while **never forwarding that held PS press to the streaming host**. Do not try to prevent Quick Actions from opening as part of this requirement.

Source change committed in `src/moonlight/vita_input.c`:
- Added a 500 ms hold threshold separate from the 250 ms double-tap window.
- If PS remains held for 500 ms, cancel the pending host-side single-press event. Releasing after a long hold also cancels the pending event, including when input polling resumes after the system UI returns.
- A short tap still waits for the 250 ms double-tap window before forwarding a single `SPECIAL_FLAG`; a second tap in that window stops the stream and suppresses the host event.
- `ps_button_capture=0` remains on the native Vita path and does not enter this state machine.

**Verification status:** source change only; no build or Vita hardware test has been run. Verify that holding PS opens Quick Actions, no PS event reaches the host after returning, a short single tap forwards exactly one event after the double-tap window, a double tap stops the stream without forwarding PS, and capture-disabled behavior remains native. If Quick Actions activates before the 500 ms threshold on target firmware, lower the hold threshold based on measured device behavior.

### Settings persistence regression isolated to feature branch (2026-10-14)

User confirms that settings persist on `master` and are lost only on `feat/quick-actions-lifecycle-review`. This narrows the issue to a branch-specific difference; do not rewrite the settings storage implementation without evidence.

Source comparison:
- `src/moonlight/settings.cpp` is byte-identical on `master` and the feature branch (same blob SHA `2acf5493b43b39df58f1af3440e54eccdcb3343c`).
- `cxml/moonlight_settings.xml` is byte-identical on both branches (same blob SHA `a67bcd123b62178f83aa9b87a2cdbd94680dd982`).
- `CMakeLists.txt` differs in SELF authorization: `master` uses `-a 0x2F00000000000101`; the feature branch experiment uses `-a 0x2800000000000001` to investigate ShellUtil PS-button capture.

This makes the branch's SELF packaging/authorization change the first controlled variable to test, but causality is not established. The `settings_version` initialization path is shared with master and is therefore not by itself an explanation for a regression that occurs only on the feature branch.

Next diagnostic:
1. Build the feature branch with the master SELF auth ID while leaving all source changes intact; test whether settings survive a full app exit/relaunch.
2. If persistence returns, investigate the packaging/auth-ID effect and choose a compatible way to support PS capture without regressing AppSettings.
3. If it still fails, compare the full build/package pipeline and other branch-specific changes, then instrument `GetInt("settings_version")`, `Initialize()`, and `SetInt` return values where the API provides them.
4. Record the tested commit, package metadata, and device result. No build or device verification has been run as part of this source comparison.


### PS capture test with master SELF auth ID (2026-10-14)

User-reported test after restoring SELF auth ID `0x2F00000000000101` while retaining `UNSAFE ATTRIBUTE 0x0E`:

- AppSettings now load and save across app restarts.
- With `ps_button_capture=1`, the log reports `sceShellUtilInitEvents=0` and `sceShellUtilLock(...PS_BTN | PS_BTN_2)=0`, so both calls return success in this run.
- Quick Actions opens when PS is held; the app does not minimize.
- Two observed presses each log `PS Button press pending tap/hold detection` followed by `PS Button hold detected; suppressing host event`. Therefore the captured presses shown in this excerpt were classified as holds and intentionally suppressed; this excerpt does not demonstrate the short-tap path or a host-side `SPECIAL_FLAG` pulse.
- The separate `[Limelight] Unrecoverable frame ... received < needed` line indicates a video-frame recovery issue and is not evidence about PS handling.

**Current diagnosis:** SELF auth ID appears to affect AppSettings persistence in the user's device test, while the PS event issue shown by these logs is consistent with the input state machine's hold classification. These are separate observations; do not assume the successful ShellUtil lock means a short PS event was sent to the host.

**Next test:** keep the working master auth ID and test a deliberately brief PS tap (release well before 500 ms), then wait at least 300 ms for the 250 ms double-tap window to expire. Capture logs for the entire press/release interval. Expected log: `PS Button single press, forwarding after timeout`. Confirm whether the host receives the mapped special/home event. Separately test a deliberate hold to open Quick Actions and confirm that no host event is sent after returning. If a short tap is still suppressed, instrument the raw PS press/release timestamps and the pending-state transitions before changing thresholds. Build and device behavior here are user-reported; no build/test was performed by the agent.


### PS short-tap test produced no expected log (2026-10-14)

User reports that the deliberate short-tap test produced none of the expected messages, including `PS Button single press, forwarding after timeout`. This does not yet identify whether the physical PS bit is visible to the polling loop, whether a press/release edge is recognized, or whether the pending state expires.

**Diagnostic change committed** in `src/moonlight/vita_input.c` (commit `2230ef398281a0595df692564066bd4f11800f50`):
- While PS capture is enabled, emit a throttled `[Input] PS diag ...` line at most once per second with raw button bits, current/previous PS state, pending flag, and press/release timestamps.
- Emit `[Input] PS Button release detected duration_us=...` when a release edge is observed.
- This should separate “PS bit never appears in the sampled controller state” from “edge/state-machine path runs but the single-tap timeout does not fire.”

**Next device test:** rebuild/install this exact commit with the known-working SELF auth ID `0x2F00000000000101`, confirm startup still reports `ps_capture=1`, tap PS briefly and capture the diagnostic lines across press and release. If `pressed` never becomes 1, investigate controller sampling/Shell interaction first. If press and release are both logged but the single-timeout message is absent, inspect timestamps and pending-state transitions before changing thresholds. If the timeout message appears but the host does not react, investigate the outgoing `SPECIAL_FLAG` pulse path. The agent has not built or tested this commit on hardware.


### PS capture diagnostic result: press observed but classified as hold (2026-10-14)

User supplied logs from the diagnostic build:

- `sceShellUtilInitEvents=0` and `sceShellUtilLock(...)=0`.
- The input loop logs `PS Button press pending tap/hold detection`, then `PS Button hold detected; suppressing host event`.
- The sampled controller state includes `raw_buttons=0x00010000 pressed=1`, followed later by a release edge. Thus PS is visible to the polling loop; this is not a total failure to sample the PS bit.
- The release diagnostic says `duration_us=0`, but this is a diagnostic artifact: the hold branch cleared `s_ps_press_time` before the release edge, so the logged duration is not a valid measurement. The excerpt does not show `PS Button single press, forwarding after timeout`.

**Diagnostic follow-up committed** in `src/moonlight/vita_input.c` (commit `fa1725ef3df560349fafe5ae6a814388ee344b01`): retain the original press timestamp after hold classification until the release edge, so the release log can report the actual sampled hold duration. This does not change the 500 ms threshold or host-event behavior.
**Next device test:** rebuild/install this commit and repeat a deliberate quick tap, releasing well before 500 ms. Check `duration_us` and whether `PS Button single press, forwarding after timeout` appears. If the measured duration is at least 500,000 µs, the input loop is genuinely observing a hold (or the app is not processing the release while the system overlay is active); then correlate the physical press/release with Quick Actions open/return before adjusting thresholds. If duration is clearly below 500,000 µs but the hold message still appears, capture the full ordered log and investigate timestamp/state transitions. Agent has not built or tested this change on hardware.

### Repeated PS taps test: timestamp discrepancy (2026-10-14)

User tested several press intervals and also a hold. The log shows only one recognized rising edge and one release edge:

- `[Input] PS Button press pending tap/hold detection`
- `[Input] PS Button hold detected; suppressing host event`
- heartbeat: `raw_buttons=0x00010000 pressed=1 old=1 pending=0 press_time=0 release_time=0`
- `[Input] PS Button release detected duration_us=0 pending=0`

The PS bit is definitely observed, but this excerpt does not demonstrate that the deliberate short taps were individually sampled: only one press/release pair appears. The recognized press was classified as a hold and the pending host event was cancelled, so no single-tap forwarding is expected from that state.

**Important source/build inconsistency:** current branch source after commit `fa1725ef3df560349fafe5ae6a814388ee344b01` explicitly retains `s_ps_press_time` after hold classification so the release diagnostic can calculate duration. In that source, the shown sequence should not produce `press_time=0` in the heartbeat while the button is still pressed, nor `duration_us=0` at release (assuming the timestamp was recorded normally). The log therefore appears to come from a build older than that fix, or from a package that does not match the current branch source. Do not tune thresholds from this duration value.

**Next test:** build/install the branch containing `fa1725ef3df560349fafe5ae6a814388ee344b01` with SELF auth ID `0x2F00000000000101`; capture the package's source commit if possible. Perform one clearly brief tap (press and release quickly), wait at least 350 ms, then do a separate deliberate hold and return from Quick Actions. For each gesture, record the ordered `press pending`, `hold detected`, `release detected duration_us=...`, and `single press, forwarding after timeout` messages. Do not change the 500 ms threshold until the measured duration and exact build are confirmed. No build or Vita test was performed by the agent.
### Research note: Vita SELF Program Authority ID / auth ID (2026-10-14)

Terminology: the 64-bit value passed with `vita-make-fself -a` is commonly called `authid`, but the PSDevWiki documents the fuller term as **Program Authority ID**. It is metadata in the SELF header, not the app's `TITLE_ID`, not a runtime Moonlight setting, and not a general-purpose list of interchangeable privilege levels. VitaSDK's `vita_create_self()` forwards configured flags to `vita-make-fself`; `UNSAFE` means the tool omits the default safe (`-s`) flag, while `ATTRIBUTE` sets a separate system-app attribute field.

Public source findings:
- PSDevWiki lists SDK-generated fSELF authority IDs `0x2F00000000000001` for SDK-compiled game applications and `0x2F00000000000002` for SDK-compiled non-game applications. This does **not** establish the meaning or safety of this project's `0x2F00000000000101` variant; no exact public match for that full value was found in this search.
- Upstream `xyzz/vita-moonlight` 0.13.2 uses `vita-make-fself -a 0x2800000000000001 -at 0x0E -c` and states this packaging change was required for PS Button capture / SceShell permission. This is an observed upstream build configuration, not proof that it is a drop-in replacement for a PAF/system-UI application.
- VitaSDK's CMake helper documentation says `UNSAFE` omits the safe flag and `ATTRIBUTE` sets the system app attribute; those controls are distinct from the authority ID.
- In this repository, `master` currently uses `0x2F00000000000101` with `UNSAFE`; the reviewed feature branch currently uses the experimental `0x2800000000000001` with `UNSAFE ATTRIBUTE 0x0E`. User testing reported settings persistence with the former and loss of persistence with the latter. This is a correlation from device testing, not a proven direct effect of auth ID alone.

Decision: keep `0x2F00000000000101` for builds where settings persistence and PAF launch behavior are required, unless a controlled test demonstrates a compatible alternative. Do not try arbitrary authority IDs copied from other applications. To investigate PS Button capture, compare otherwise-identical packages, inspect generated SELF metadata, and test PAF launch, settings persistence, `sceShellUtilLock`, PS behavior, and exit/resume on the target Vita. Record the exact source commit and package used for each result.

Sources reviewed:
- VitaSDK `vita.cmake`: https://github.com/vitasdk/vita-toolchain/blob/master/cmake_toolchain/vita.cmake
- Upstream Moonlight build: https://github.com/xyzz/vita-moonlight/blob/vita/CMakeLists.txt
- Upstream Moonlight 0.13.2 notes: https://github.com/xyzz/vita-moonlight/blob/vita/README.md
- PSDevWiki Program Authority ID: https://www.psdevwiki.com/ps3/Program_Authority_ID

### Restore the PAF-compatible SELF authority ID for continued PS tests (2026-10-14)

**Change committed:** `CMakeLists.txt` now uses `-a 0x2F00000000000101` again while retaining `UNSAFE ATTRIBUTE 0x0E`. Commit: [7eb5086](https://github.com/NowaLone/vita-moonlight-paf/commit/7eb5086a5c2dc087029d328db03ce00875c40e3a).

Reason: the user-reported device test found AppSettings persistence working with this authority ID and reported successful `sceShellUtilInitEvents` / `sceShellUtilLock` return codes in the same configuration. The upstream `0x2800000000000001` experiment coincided with settings persistence failing, so it is not a suitable default for further input testing without resolving that regression. This is a controlled project decision based on reported device results, not proof that the authority ID alone caused the persistence difference.

**Next device verification:** build and install this exact branch revision; verify settings survive app restart, record the source commit shown by About if available, and test one brief PS tap (release well before 500 ms, then wait at least 350 ms) separately from a deliberate hold that opens Quick Actions. Capture ordered press/release/timeout logs and confirm whether the PC receives the short-tap special/home event. Do not tune the 250 ms double-tap window or 500 ms hold threshold until the installed revision and measured press duration are known.

**Not verified by the agent:** no build or Vita hardware test was run for this commit.


### PS input is absent from latest log; compare controller polling APIs (2026-10-14)

User reports that pressing PS together with Select to forcibly close the app results in a PS/home event reaching the streaming PC. In the supplied streaming log, however, every throttled PS heartbeat shows `raw_buttons=0x00000000`; no press-edge, release-edge, hold, or single-tap-timeout message appears. The log excerpt does not include the forced-close moment, so the route by which the PC receives that event remains unconfirmed.

Reference: VitaSDK documents `sceCtrlPeekBufferPositiveExt2` as the wireless-controller polling API, while `sceCtrlPeekBufferPositiveExt` is the regular controller polling API. The project currently uses Ext2 for its main input sample. This is a reason to compare APIs, not yet proof that Ext2 is the bug: earlier user logs did show `raw_buttons=0x00010000` at least once.

**Diagnostic change committed:** [34f2898](https://github.com/NowaLone/vita-moonlight-paf/commit/34f2898d7cd797ac64127df0008fb89e0783375a) keeps Ext2 as the actual game-input source, additionally polls Ext into a separate probe buffer, logs both API return values and button masks in the heartbeat, and logs Ext probe press/release edges. This is diagnostic-only and does not intentionally change which controller state is forwarded to the host.

**Next device test:** install a build containing this exact commit. While streaming, test a short PS tap, a deliberate PS hold, and PS+Select separately. Capture `PS diag Ext2 result=...`, `PS probe Ext edge...`, and the existing state-machine messages. Interpret the results:
- Ext sees PS but Ext2 does not: consider switching the main sample to Ext after checking L/R trigger mapping and ordinary controller behavior.
- Neither API sees PS while the lock calls succeed: investigate ShellUtil interception/permissions and firmware behavior; don't tune tap timing.
- Either API sees PS edges but the state machine does not classify them: inspect the state machine and timestamps.
- The PC receives PS only as the app is forcibly closed: capture host-side event timing and test whether app shutdown/release behavior explains it.

No build or hardware test was performed by the agent.

Sources:
- [VitaSDK SceCtrl user API docs](https://docs.vitasdk.org/group__SceCtrlUser.html) — distinguishes regular and wireless Ext polling APIs and notes PS Button is a controller button.
- [VitaSDK ShellUtil API docs](https://docs.vitasdk.org/shellutil_8h.html) — defines PS Button and Quick Menu lock types.

### PS polling comparison results (2026-10-14)

User supplied a new log from the build containing the Ext2-vs-Ext probe:

- Both polling calls return 1 and report the same button mask in the sampled heartbeat. When PS is idle, both report buttons=0x00000000; during observed presses, both report buttons=0x00010000.
- sceCtrlPeekBufferPositiveExt() logs both rising and falling edges. The main Ext2 sample and the separate Ext probe therefore agree for the observed PS gestures; this log does not support switching the main input source from Ext2 to Ext.
- Two observed holds were classified as holds and suppressed from host forwarding. The release diagnostics report duration_us=2218752 (about 2.22 s) and duration_us=1301405 (about 1.30 s), both above the 500 ms hold threshold. The second press initially appears as pending and is later cancelled by hold detection.
- No “PS Button single press, forwarding after timeout” message appears in this excerpt, so it does not verify the short-tap-to-host path.
- The excerpt does not explicitly capture the PS+Select forced-close moment or show a host-side PS event. It therefore does not establish why a PS event reaches the PC during forced close.

**Next diagnostic:** do one deliberately brief tap while keeping the stream active, release well before 500 ms, then wait at least 350 ms. Confirm that the log shows a press edge, release edge with duration_us clearly below 500000, and “PS Button single press, forwarding after timeout”; verify whether the PC receives the expected special/home event. Then separately test PS+Select forced close and note whether the host receives PS before, during, or after app termination. Do not change the polling API or timing thresholds based on this excerpt alone.

The repeated “Waiting for IDR frame” messages and later decoded-frame counters are a separate video recovery/performance concern; this log does not correlate them to the PS state machine. No build or hardware test was performed by the agent.


### Short PS tap not handled: combine polling samples (2026-10-14)

User reports that short PS taps are not handled. The current state machine already supports a short-tap path, but its edge detection used only the PS bit in the Ext2 sample even though the loop also polls Ext into a separate buffer.

**Source change committed:** [f82f3a6](https://github.com/NowaLone/vita-moonlight-paf/commit/f82f3a6e9ccd27ff0c9b9ab30786a554915f66ea) updates PS detection to use the OR of the PS bit from Ext2 and Ext. Ext2 remains the source of all normal controller buttons; only PS edge detection combines the samples. The heartbeat's `effective_pressed` field now shows the combined state, while the raw button masks remain available for comparison.

This is a targeted attempt to avoid missing a brief PS state visible in only one polling sample; it is not yet proven to be the root cause. It does not change the 500 ms hold threshold or the 250 ms double-tap window.

**Device verification needed:** build/install this exact commit with the known-working SELF auth ID `0x2F00000000000101`. Test one brief PS tap, wait at least 350 ms, then test a hold separately. Expected short-tap sequence: `press pending` → `release detected duration_us<500000` → `single press, forwarding after timeout`. If that sequence appears but the host does not react, investigate the `SPECIAL_FLAG` send path; if it does not, capture the new `effective_pressed` and both raw API masks. No build or Vita hardware test has been run by the agent.


### Combined PS polling test: no press edge observed (2026-10-14)

User tested the build containing the combined Ext2/Ext PS detection and supplied a log excerpt. The startup confirms `ps_capture=1`; ShellUtil init and lock both return 0. However, every included heartbeat reports Ext2 and Ext button masks as `0x00000000`, `effective_pressed=0`, `old=0`, and `pending=0`. No Ext probe edge, state-machine press/release message, or single-tap timeout appears.

**Interpretation:** this excerpt does not show the state machine receiving any PS tap, so it cannot be evidence of a timeout or SPECIAL_FLAG-send failure. It is consistent with the polling APIs not exposing PS during the attempted taps, but the excerpt alone cannot prove whether the taps occurred during the captured interval or whether the app/firmware consumed them. Successful ShellUtil return codes confirm only that those calls returned success, not that PS samples must be visible through SceCtrl.

**Next diagnostic:** capture a tightly scoped log while deliberately holding PS down for about one second, then releasing, with the console log visible continuously. Confirm whether either API ever reports `0x00010000` or emits `PS probe Ext edge`. This is a control test because previous logs did register held PS presses. If a hold is also invisible in the same installed build, verify the exact package/source revision and investigate ShellUtil/foreground behavior before changing tap timing or SPECIAL_FLAG code. If the hold is visible but a short tap is not, compare the raw edge logs and measured tap duration. The repeated `Waiting for IDR frame` messages are video recovery diagnostics and do not explain the absent PS samples. No build or hardware test was performed by the agent.

### Three-path PS behavior (2026-10-14)

User observation on the current build:

1. A single PS tap is blocked: the app does not minimize, the tap is absent from the logs, and nothing reaches the host.
2. A PS hold opens Quick Actions, is present in the logs, does not minimize the app, and correctly sends nothing to the host.
3. PS+Select force-closes the app. That combo is not blocked and is absent from the logs, but a press still reaches the host.

**Interpretation:** the ShellUtil lock is doing its system-side job for a tap, but the short press never enters the SceCtrl sample the state machine watches. A hold is long enough to appear in SceCtrl, so the existing hold-suppression path runs. PS+Select is a system combo that bypasses the app lock and kills the process before this loop can log; the host press is therefore not evidence that the tap state machine sent `SPECIAL_FLAG`. Clean stream stop already sends a zero controller packet, so this is an abrupt-kill/disconnect side effect.

**Source change:** register `sceShellUtilRegisterEventHandler` and treat a `PS_BTN`/`PS_BTN_2` event as a synthetic short-press edge when SceCtrl did not see the bit. Also scan the recent Ext2 history in case the newest sample is already clear. Expected short-tap log is `ShellUtil event` or `PS found in Ext2 history`, then `press pending`, `release detected`, and `single press, forwarding after timeout`. No hardware test has been run for this change.

### Short PS taps still absent after combined polling/history probe (2026-10-14)

User explicitly tested several short PS taps while streaming and supplied a continuous console excerpt. In every included diagnostic heartbeat:

- `sceCtrlPeekBufferPositiveExt2` and `sceCtrlPeekBufferPositiveExt` return 1, but both button masks are `0x00000000`.
- `effective_pressed=0`, `old=0`, and `pending=0`; no press/release edge reaches the tap/hold state machine.
- There is no `PS probe Ext edge`, `PS found in Ext2 history`, `PS edge from ShellUtil event`, or state-machine press/release/timeout message.
- The repeated ShellUtil callback logs have `result=0 mode=21 type=0x00000000`. They are not evidence of a PS-button event: the callback's `type` field contains no `PS_BTN` or `PS_BTN_2` bit.

This confirms that the current tap state machine and its timeout are not the immediate failure point: the attempted taps are not visible to any of the currently logged detection paths. The excerpt cannot distinguish firmware/ShellUtil interception from the controller API simply not exposing those brief taps.

**API research:** VitaSDK's `psp2/ctrl.h` documents `sceCtrlPeekBufferPositiveExt2` as polling and allows requesting up to 64 samples; `sceCtrlReadBufferPositiveExt2` is explicitly documented as blocking. Do not call the blocking read from the existing 2 ms input loop without a separate design, because it may stall input processing. The current 16-sample `PeekBuffer` history scan produced no PS-history log in this test, so it has not recovered the short taps.

**Next investigation:** find a non-blocking, independently observable PS-button event source or verify the exact semantics of the Ext2 sample history on real Vita firmware. Keep the existing normal controller path and proven hold suppression unchanged until a source-level event can be demonstrated. Do not ask the user to repeat the same short-tap or hold tests; the current evidence already shows short taps missing and earlier logs already confirm hold detection.

References:
- [VitaSDK SceCtrl user API](https://docs.vitasdk.org/group__SceCtrlUser.html) — polling and buffered-read declarations.
- [VitaSDK `ctrl.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/ctrl.h) — Peek APIs are polling; Read APIs are documented as blocking.
- [VitaSDK ShellUtil API](https://docs.vitasdk.org/shellutil_8h.html) — ShellUtil callback carries lock mode/type, not a documented raw PS press/release event stream.

No build or Vita hardware test was performed by the agent.

### ShellUtil callback correlation diagnostics (2026-10-15)

**Diagnostic-only source change committed:** [0eeffe2](https://github.com/NowaLone/vita-moonlight-paf/commit/0eeffe23ad844b915389249f7411027a3b0df25b) adds a monotonically increasing callback counter and sceKernelGetSystemTimeWide() timestamp to each ShellUtil callback log. The log now prints count, time_us, result, decimal and hexadecimal mode, and hexadecimal type.

Why: user observed repeated callbacks near short PS taps, but the captured arguments were consistently result=0 mode=21 type=0x00000000. A zero type contains neither documented PS_BTN nor PS_BTN_2, so the existing handler correctly does not synthesize a PS edge from that callback. Decimal and hexadecimal mode output will make the unexpected value easier to compare with the VitaSDK enum declarations; timestamps/counters let the next log establish event order and timing rather than relying on visual proximity.

**Next device check:** build/install this exact revision and make one brief PS tap while the stream log is continuously visible. Capture the callback line(s) immediately before and after the tap plus the PS heartbeat. Do not repeat the already-confirmed hold test. Compare whether callback timestamps/counts cluster reproducibly around taps and whether result, mode_hex, or type changes. If callback arguments remain unchanged, do not treat type=0 as a PS event; investigate callback registration/API semantics and another independently observable event source. If a documented PS type bit appears, wire only that verified signal into the existing edge/state-machine path.

The change deliberately does not interpret type=0 as a PS tap and does not alter controller polling, tap timing, or hold suppression. No build or Vita hardware test was performed by the agent.

### ShellUtil mode=21/type=0 callback follows repeated PS taps (2026-10-15)

User tested many short PS taps with timestamped callback diagnostics. The first callback was `result=0 mode=1 type=0x00000801`, followed by `PS edge from ShellUtil event`, press-pending, release after 2714 µs, and the single-tap timeout. This confirms the documented PS lock-type path can inject a pulse into the existing state machine. The 2714 µs interval is the synthetic pulse duration, not a measurement of the physical button hold.

The remaining callbacks (counts 2–15) repeatedly reported `result=0 mode=21 mode_hex=0x00000015 type=0x00000000` while the user was tapping PS. VitaSDK publicly documents lock modes 1 and 2 and does not document mode 21 as a PS event, so this signature is not yet a confirmed API contract. However, its repeated occurrence during the tap test makes it a useful candidate for a controlled experiment.

**Experimental source change committed:** [0bba74e](https://github.com/NowaLone/vita-moonlight-paf/commit/0bba74e68b1b800f65139edef1327f8bc438378d) provisionally latches a PS pulse for `result=0, mode=21, type=0`, with a separate log tag `ShellUtil PS edge source=experimental_mode21`. The documented PS_BTN/PS_BTN_2 type-bit path remains unchanged. This is intentionally experimental: do not treat mode 21 as a documented enum value until hardware results establish the mapping.

**Next device check:** build/install this exact revision and test a few single taps, then one hold only if needed. For each tap, look for `ShellUtil PS edge source=experimental_mode21`, `PS Button press pending`, `PS Button release detected`, and `PS Button single press, forwarding after timeout`; also confirm whether the host receives exactly one home/special event per tap. If unrelated ShellUtil events also trigger the experimental path, revert this heuristic and investigate the callback ABI/event semantics instead. Existing hold suppression is intended to remain unchanged, but this experimental signature should be validated before relying on it.

Reference: [VitaSDK ShellUtil API](https://docs.vitasdk.org/shellutil_8h.html) documents lock modes 1/2 and PS lock type bits 0x1/0x800; it does not document mode 21/type 0 as a PS event. No build or Vita hardware test was performed by the agent.

### Short PS tap reaches the streaming host (2026-10-15)

User tested several short PS taps on the build containing the experimental `mode=21/type=0` handler and confirmed that the PS/special-button action reaches the PC.

The supplied device log shows five callbacks (`count=2` through `count=6`) with `result=0 mode=21 type=0x00000000`. Each is followed by `ShellUtil PS edge source=experimental_mode21`, a synthetic press/release pulse (about 2.7–3.1 ms), and `PS Button single press, forwarding after timeout`. Both SceCtrl polling APIs continue to report `buttons=0`, so short-tap detection is coming from the ShellUtil callback path rather than the controller samples. The user confirms the host receives the action; this validates the experimental path for the tested short taps.

**Remaining caveats and next check:**
- `mode=21/type=0` remains undocumented by the public VitaSDK ShellUtil enum, so keep the special case isolated and do not assume it represents a PS tap for every firmware or ShellUtil callback.
- Verify one deliberate PS hold that opens Quick Actions: it must not send a host-side PS action, and normal controller forwarding must behave correctly around opening/closing the overlay.
- If that passes, consider the short-tap path hardware-verified for this tested device/build, while keeping Quick Actions lifecycle and firmware portability open.
- No build was run by the agent; the report describes the user's test of their installed build.


### PS hold suppresses host event; return/input lifecycle still needs confirmation (2026-10-15)

User tested deliberate PS holds several times on the same device build and reports that **no extra PS action is sent to the streaming host**.

The supplied logs support the hold-suppression result:
- The controller poll detects buttons=0x00010000, then logs `PS Button hold detected; suppressing host event`.
- Release is eventually observed with durations around 1.85–3.02 seconds; no host-forwarding log appears in the supplied hold segments.
- Video decoding and stream diagnostics continue in the capture. One `[Stream] connection status=0` appears during the first hold, but this excerpt alone does not establish whether it means an expected pause during the system overlay or a stream disconnect.
- After the PS release edge, `sceCtrlPeekBufferPositiveExt2` history repeatedly reports `PS found in Ext2 history index=0 buttons=0x00010000` even while current samples show `buttons=0`. This is stale/repeated history output and should not be interpreted as repeated physical presses. It is noisy and worth fixing or throttling separately so diagnostics reflect current state more clearly.

**Assessment:** host-side hold suppression is confirmed by the user's repeated tests. The log is not enough to call the complete Quick Actions lifecycle verified: it does not explicitly prove that controller forwarding resumes correctly after returning from the overlay, and the meaning of `connection status=0` needs context from the surrounding stream lifecycle code/logs. Keep lifecycle handling open until return-to-app and ordinary controller input are explicitly confirmed. The experimental ShellUtil `result=0, mode=21, type=0` signature also remains undocumented; these hold logs do not establish its behavior on other firmware.

No build or Vita hardware test was performed by the agent; these observations are from the user's device test.


### Gate host input while Quick Actions/system UI is overlaid (2026-10-15)

**Source change committed for device validation:** [598c923](https://github.com/NowaLone/vita-moonlight-paf/commit/598c923e0adbf473e366d366329395b840a3d649) adds a `SceAppMgrAppState` poll via `_sceAppMgrGetAppState(..., sizeof(state), 0)` and checks `isSystemUiOverlaid` at a 100 Hz cadence before entering the normal input processing path.

When the system UI overlay is reported active, the input loop sends one neutral controller packet to release any host-held controls, updates local controller/touch baselines without forwarding them, clears pending PS tap/pulse state, skips normal controller/touch forwarding, and returns early. Baselines continue to update during the overlay so button presses made and released inside Quick Actions are not replayed to the host after returning. A known active-overlay state is preserved across transient AppMgr API errors; if the API has never succeeded, the code fails open and logs the error so the app does not silently remain input-locked.

**Important limitations:** this is a source-level implementation, not yet device-verified. It depends on the target firmware returning a valid `isSystemUiOverlaid` value from `_sceAppMgrGetAppState`. The 10 ms poll interval also means the first few milliseconds of an overlay transition could precede detection. The error log must be checked on hardware; if AppMgr returns an error, do not consider overlay suppression verified. No build was run by the agent.

**Next device check:** install a build from this commit, open Quick Actions with a PS hold, press/release several ordinary buttons while the overlay remains open, and confirm none of those actions reaches the PC. Close Quick Actions, then confirm ordinary input works again without restarting the stream. Capture the `[Input] System UI overlay changed active=1` / `active=0` messages and any `AppMgr get app state failed` lines. Also watch whether `[Stream] connection status=0` recurs and correlate it with the overlay state rather than assuming it means a disconnect.

Reference implementation: [GrapheneCt/TransparentImpose](https://github.com/GrapheneCt/TransparentImpose/blob/master/TrImpose.c) polls `SceAppMgrAppState.isSystemUiOverlaid` through `_sceAppMgrGetAppState`; the public VitaSDK header declares `_sceAppMgrGetAppState` and defines `SceAppMgrAppState` as a 0x80-byte structure. This supports the API/structure choice, but does not by itself prove behavior on every retail firmware.

### Suppress the stale PS edge after Quick Actions closes (2026-10-15)

User verified that ordinary controller input is blocked while Quick Actions is open and resumes after closing, but reported one extra PS action reaching the host on return. The supplied log narrows the sequence down:

- AppMgr reports `active=1`, and the host controller state is released.
- AppMgr then reports `active=0`.
- Immediately after returning, Ext/Ext2 reports a fresh `SCE_CTRL_PSBUTTON` bit and the PS state machine starts a new pending tap.
- The next history log still contains the old PS bit, consistent with the already-observed stale Ext2 history behavior.

**Source change committed for device validation:** [0956be8](https://github.com/NowaLone/vita-moonlight-paf/commit/0956be88d76434aa4541a80d180fa366b9b2347b) adds a post-overlay PS quarantine. While Quick Actions is active, the input path arms the quarantine. After the overlay closes, it masks PS from both current polling samples and clears pending PS state until both current samples report PS released. It also skips the Ext2 history fallback during this transition, so a stale buffered PS bit cannot recreate the synthetic press. Other controller buttons continue through the normal path; the quarantine does not intentionally block ordinary input after returning.

**Device validation needed:** rebuild/install this exact commit, open Quick Actions with a PS hold, then close it. Confirm there is no host-side PS action on return, while ordinary buttons are still blocked inside the overlay and resume after it closes. Check for `[Input] PS suppression after system UI cleared`; an `Ignoring PS sample while returning from system UI` line is expected only if the current PS bit remains visible briefly after the overlay closes. Then test a deliberate short PS tap after the quarantine has cleared to ensure the normal host-side tap path still works.

No build or hardware test was performed by the agent.

#### Follow-up: Ext2 history is persistently stale on the tested device

Reviewing the return-to-app trace exposed a second way the extra PS action could recur: even after current Ext/Ext2 samples report PS released, the Ext2 history path has repeatedly returned the old `0x00010000` bit at index 0. Merely skipping history for the first post-overlay sample would leave later iterations vulnerable to replaying that stale bit.

**Additional source change:** [4f5daf9](https://github.com/NowaLone/vita-moonlight-paf/commit/4f5daf931bb89d1bee70afa18ee0d857275193ab) removes the Ext2 history fallback from PS edge detection. The existing ShellUtil callback path remains responsible for the tested short-tap path; current Ext/Ext2 samples continue to support hold detection. This intentionally favors avoiding false PS presses over trying to reconstruct a tap from history that the device has demonstrated can remain stale.

The post-overlay quarantine from [0956be8](https://github.com/NowaLone/vita-moonlight-paf/commit/0956be88d76434aa4541a80d180fa366b9b2347b) remains in place. Re-test both cases on hardware: closing Quick Actions must not emit a host PS action, and a deliberate short PS tap after returning must still reach the host through the ShellUtil path. No build or hardware test was performed by the agent.

### Quick Actions return: stale PS sample is suppressed (2026-10-15)

User tested the post-overlay quarantine on Vita and reports that behavior now appears correct. The supplied log confirms:

- AppMgr transitions to `active=1`, and the host controller state is released.
- On return, AppMgr transitions to `active=0`.
- Ext briefly reports `buttons=0x00010000` while Ext2 reports `buttons=0x00000000`.
- The input loop logs `Ignoring PS sample while returning from system UI` rather than starting the PS tap state machine.

This is direct device evidence that the stale/late PS sample is being filtered on return. User's assessment is "вроде всё ок"; keep this marked as user-tested on the current device, not universally verified across firmware. The remaining quick regression check is to confirm that a deliberate short PS tap after the quarantine clears still reaches the host and ordinary controller buttons continue to work. No build or hardware test was performed by the agent.

### Quick Actions lifecycle regression check complete on user's device (2026-10-15)

User confirms that a deliberate short PS tap after returning from Quick Actions still reaches the streaming host. Together with the preceding device log showing the stale PS sample being ignored during return, and the earlier confirmation that ordinary buttons resume after closing the overlay, the currently tested lifecycle behaves as intended on this device:

- Ordinary controller input is suppressed while Quick Actions/system UI is overlaid.
- Closing the overlay does not forward the stale PS sample as an extra host action.
- Ordinary controller input resumes after return.
- A deliberate short PS tap after the quarantine clears still reaches the host.

This is user-reported device verification for the tested build/device/firmware only; it is not cross-firmware verification. No build or hardware test was performed by the agent.



### Remove temporary PS input diagnostics before integration (2026-10-15)

Removed the temporary ShellUtil callback counter/timestamps, per-callback argument dump, per-sample Ext edge logs, and one-second PS polling heartbeat from `src/moonlight/vita_input.c`. These were added to identify the short-tap event source and are no longer needed after the tested device confirmed the ShellUtil path and Quick Actions lifecycle. Kept event-driven PS state-transition messages and AppMgr overlay/error messages because they remain useful for diagnosing real input/lifecycle regressions. The undocumented `result=0, mode=21, type=0` heuristic and post-overlay quarantine are unchanged; this is log cleanup, not a behavior change.

No build or hardware test was run for this cleanup.


## GameStream client API coverage and follow-up (2026-10-15)

Scope decision: this project implements the **Moonlight/GameStream client** required to discover hosts, pair, list apps, launch/resume, and stream. Sunshine's administrative Web API (server configuration, user/admin management, and similar control-plane features) is out of scope and is not a missing feature for the streaming client.

Source audit of `src/moonlight/api.cpp`, `src/backend/legacy/legacy_gamestream.c`, and `src/moonlight/stream_session.cpp`:

- Implemented client flow includes server status/info, GameStream pairing, app-list retrieval, launch/resume request, and Limelight RTSP streaming setup/teardown.
- No separate HTTP `/cancel`, `/quit`, or `/unpair` request is currently issued. Do not add these endpoints by name alone: first verify the current Sunshine/Moonlight protocol semantics and whether the existing Limelight teardown and pairing lifecycle already cover the intended user action.
- The app-list parser previously stopped after eight entries. The client cache/parser limit has now been raised to 64 to avoid truncating ordinary larger app lists. This remains an explicit bound; a future change can make storage dynamic if hosts with more than 64 apps are a real requirement.
- Streaming capability negotiation remains intentionally conservative: H.264 only, stereo audio, one remote controller, and fixed Rec.601/limited-range color settings. HEVC/AV1, multichannel audio, HDR, and multi-controller support require checking Vita decoder/renderer/input capabilities before advertising or enabling them.
- Server metadata has fallback values when some fields are absent. Keep fallback behavior compatible with older GameStream hosts, but avoid treating fallback values as negotiated capability data.

Next work, in order:

1. Verify stop/disconnect behavior against current upstream client implementations and host-side session cleanup; make client teardown idempotent where needed before adding protocol calls.
2. Add focused tests for app-list parsing (more than eight apps, malformed/incomplete entries, and truncated responses) and decide whether the 64-entry bound is sufficient.
3. Implement codec/audio/color negotiation only after confirming Vita hardware and renderer support. Do not advertise a codec that the decoder cannot actually consume.
4. Keep Sunshine administrative Web API support out of the client roadmap unless the product scope explicitly changes.

**Verification status:** source changes and this audit have not been built or exercised by the current agent. A clean Vita toolchain build and real-device streaming/lifecycle test are still required.


### Stream lifecycle follow-up: reject duplicate start while pending (2026-10-15)

Source change: [54ba678](https://github.com/NowaLone/vita-moonlight-paf/commit/54ba67847c49b883e9f6f03dce8f8f20e732f0b3) adds explicit `s_stream_pending` and `s_stream_active` checks to `moonlight_api_start_application()`. The connection state can remain `PAIRED` during the asynchronous launch handshake, so state-only validation was not sufficient to express the stream lifecycle invariant. The API now rejects a second start request while a stream start is pending or a stream is active, rather than relying only on the lower-level worker to reject it.

**Resolution:** the teardown race and pending-launch path are addressed in the later section **Deferred stream teardown on host disconnect** below. That implementation is source-reviewed but still requires a build and Vita device regression test.


### Deferred stream teardown on host disconnect (2026-10-15)

Implemented on `master` to address the detached-worker lifetime race:

- `moonlight_api_disconnect_host()` now requests stream cancellation and schedules deferred cleanup instead of immediately freeing `LegacyGameStreamServer`.
- The cleanup worker waits until the detached stream worker has fully returned, then shuts down GameStream resources and emits `MOONLIGHT_EVENT_CONNECTION_CLOSED`. Repeated disconnect calls while cleanup is pending are idempotent.
- `moonlight_stream_stop()` now records cancellation during the launch handshake without prematurely calling `LiStopConnection()`; the stream worker checks cancellation after the launch HTTP request and again before entering `LiStartConnection()`.
- Stop is accepted while a launch is pending, not only after `STREAM_STARTED`.
- API shutdown requests cancellation and waits for the stream worker before releasing server resources. A new host connection is rejected while stream/disconnect cleanup is still in progress.

No Sunshine-specific cancel/quit HTTP endpoint was added. A launch request already accepted by the host may still start the remote application; this client-side cancellation only prevents the local worker from continuing into RTSP when cancellation is observed.

**Verification status:** source review only; no build or Vita hardware test was run for this change. Please build and test disconnect both during active streaming and immediately after launch, before treating the lifecycle as device-verified.


### Build follow-up: stream stop worker symbol scope (2026-10-15)

- Fixed the C++ compile error in `stream_session.cpp`: `stream_stop_thread_main()` is now defined inside the same anonymous namespace as its callers, avoiding two distinct functions with the same name and the ambiguous `pthread_create` callback.
- Fixed the Vita SDK OpenSSL warning in `legacy_gamestream.c` by matching `X509_get0_signature()`'s declared `ASN1_BIT_STRING **` parameter type. The pointer is only read by this code.
- Verification: source changes committed; no local build was run by the agent. Rebuild with VitaSDK to confirm.


### Fix double-PS stream stop after launch worker exits (2026-10-16)

User supplied a device log where the input state machine correctly logged `[Input] PS Button double press, stopping stream`, but video decoding continued and no stop-worker diagnostics appeared.

Source review identified a lifecycle mismatch in `src/moonlight/stream_session.cpp`: `moonlight_stream_stop()` returned immediately when `s_stream_thread_running` was false, even if `s_stream_context.started` still represented an established Limelight connection. The detached launch/setup worker is not a reliable proxy for the active stream lifetime.

**Fix committed to master:** [4e85124](https://github.com/NowaLone/vita-moonlight-paf/commit/4e851246e2a60ac369eccd23a5a221d0d8a6e212)

- `moonlight_stream_stop()` now proceeds when either setup is pending/running or the connection is established; it only returns early when neither state is active.
- `moonlight_stream_is_running()` now includes the established connection and stop-request state, so deferred disconnect cleanup does not mistake an active Limelight session for an idle one just because the launch worker returned.
- The existing pending-launch cancellation path remains in place; the fix does not add a Sunshine-specific HTTP endpoint.

**Device regression test required:** rebuild/install this commit, start a stream, double-press PS, and check for `[Stream] stop worker launched`, `[Stream] stop worker start`, `[Stream] emitting manual STREAM_STOPPED`, and `[Stream] stop worker finished`. Video frame logs should stop, the UI should leave the streaming state, and starting the same host/app again should work. Also test disconnect during an active stream to ensure deferred cleanup waits correctly.

No build or hardware test was run by the agent; only the source change was committed.


### Prevent stale PS callbacks from stopping a newly restarted stream (2026-10-16)

User verified that double-PS now stops the active stream, but the immediate attempt to start the stream again appeared to stop almost immediately. The supplied trace shows the second launch reaching `[Stream] connection started`, followed by a fresh PS edge, a pending tap, another edge, and `PS Button double press, stopping stream`. This points to a stale/late ShellUtil callback being replayed into the next session, rather than a failure to launch the host application.

**Fix committed to master:** [ef4b777](https://github.com/NowaLone/vita-moonlight-paf/commit/ef4b777a92022e850ba887cbd9ae8e368d303a9d)

- `vita_shell_event_handler()` now ignores ShellUtil PS callbacks while `s_input_active` is false, so callbacks during teardown cannot queue a new edge.
- `vita_input_start()` and `vita_input_stop()` explicitly clear `s_shell_ps_edge` and reset PS tap/hold state so an edge cannot carry over between stream sessions.
- Reset the post-system-UI PS suppression flag on stream start/stop to avoid carrying unrelated overlay state across a full stream lifecycle.

**Device regression test required:** rebuild/install this commit; start a stream, double-press PS to stop it, then restart the same app. Verify the new stream stays active for at least 30 seconds and does not log a PS edge/double-press stop unless PS is deliberately pressed again. Also verify a deliberate short PS tap still reaches the host and a deliberate double press still stops the stream.

No build or device test was performed by the agent; this is a source-level fix based on the supplied trace.


### Reject undocumented ShellUtil callbacks as synthetic PS presses (2026-10-16)

User confirmed that no physical PS button was pressed when the restarted stream logged:

`[Input] PS edge from ShellUtil event`
`[Input] PS Button press pending tap/hold detection`
`[Input] PS Button release detected duration_us=3121 pending=1`
`[Input] PS Button single press, forwarding after timeout`

The video stream continued in this capture, but the input trace proves that an unsolicited ShellUtil callback was interpreted as a real PS tap. The current handler accepted either documented PS lock-type bits or the undocumented signature `result=0, mode=21, type=0`. VitaSDK documents lock modes as `LOCK=1` and `UNLOCK=2`, and PS lock types as `SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN` / `SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2`; the observed signature does not identify a PS lock type.

**Fix committed to master:** [f4ebee1](https://github.com/NowaLone/vita-moonlight-paf/commit/f4ebee1320e87b5eedee6e92c1067152c0a2dd6c)

- Removed the undocumented `result=0, mode=21, type=0` fallback from `vita_shell_event_handler()`.
- ShellUtil can now enqueue a PS edge only when the callback's type explicitly contains one of the PS-button lock flags.
- This favors preventing false PS actions on stream restart over trusting an undocumented callback signature. A short physical tap that is too brief for controller polling may no longer be detected if the firmware does not provide the documented PS type; verify this on hardware before calling PS tap handling complete.

**Device regression test required:** rebuild/install this commit, stop a stream with double-PS, restart the same app without touching PS, and wait at least 30 seconds. There must be no PS edge/tap/single-press logs unless PS was physically pressed. Then test a deliberate short PS tap and a deliberate double press. If the short tap is missed, capture callback arguments for an actual tap and an idle/restart period so the event source can be distinguished without treating arbitrary callback signatures as PS input.

No build or hardware test was performed by the agent. Reference: [VitaSDK ShellUtil documentation](https://docs.vitasdk.org/shellutil_8h.html).


### Diagnose PS taps after removing undocumented ShellUtil fallback (2026-10-16)

After [f4ebee1](https://github.com/NowaLone/vita-moonlight-paf/commit/f4ebee1320e87b5eedee6e92c1067152c0a2dd6c), the user reports that deliberate PS taps no longer react. The supplied trace contains one `PS edge from ShellUtil event` and one single-press forwarding message immediately after stream startup, before the first video packet, but no evidence that later physical taps reached the input state machine. This suggests the remaining accepted callback may be related to the ShellUtil lock/startup transition rather than a physical tap; the existing log does not include callback arguments, so that is not yet proven.

**Diagnostic commit:** [a4aba5e](https://github.com/NowaLone/vita-moonlight-paf/commit/a4aba5e7728a007289cfaa4ddf419fd568fd5c6b)

- Added a temporary log of every ShellUtil callback's `result`, `mode`, `type`, and whether streaming input is active.
- The PS state machine still accepts only callback types containing documented PS lock-type flags; the undocumented `result=0, mode=21, type=0` fallback remains disabled.
- This is diagnostic only; it does not restore the unsafe fallback or change input behavior.

**Next device capture:** install a build containing this commit. Capture the callback line(s) emitted immediately after stream startup, then press PS once, release, and press it again. Compare `result/mode/type` for startup versus each deliberate tap. If no callback appears on taps, inspect current controller polling samples next; do not infer that lock/unlock callbacks are physical button events.

No build or hardware test was performed by the agent.

## PS Button ShellUtil callback compatibility (2026-10-16)

- Restored compatibility with the observed Vita callback signature `result=0, mode=21, type=0`, which the user confirmed occurs on short physical PS Button presses. Filtering exclusively by documented PS lock-type flags caused these short presses to stop being handled.
- Kept acceptance of callbacks whose type contains `SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN` or `SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2`.
- The separate startup callback `mode=1, type=0x00000801` was reported to have no observable effect; it is not treated as the cause of a stream problem.
- Hardware verification is still required after this source change: check short and held PS presses, double-PS stop, and stream restart. No build or device test is claimed by this change.
