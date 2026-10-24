# Focused checks

Latest recorded source gate: **206 checks including stream warnings (2026-10-24)**.
See [the repeat Ponytail audit](../docs/PONYTAIL_REAUDIT_2026-10-23.md) for the latest
baseline, per-change checks and native gates; [the earlier refactor audit](../docs/REFACTOR_AUDIT.md)
records the preceding pass. Counts in the
individual correction records below belong to those historical source stages. Current test
output is authoritative for any later checkout. Device evidence and remaining acceptance
cases are in the API audit and `runtime/README.md`; source checks do not replace those gates.

Non-build checks (Python 3 and pycparser):

    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py'

`test_stream_warnings.py` adds five non-build checks interpreting the actual warning policy,
receive-counter/status functions and PAF badge state transitions. They cover healthy low-bitrate
scenes, loss/bitrate/stall/startup boundaries, receive metrics without detailed statistics,
stale/inactive generations, reconnect reset, stop/cancel guards, warning changes, three-second
recovery hold and immediate stop hiding. Resource labels and polling wiring are source-checked.
Clock, bucket advancement, mutexes, memory clearing and PAF are serial mocks; detailed decoder/GPU
timing, native ABI, real packet recovery/concurrency, RCO layout and Vita behavior are unverified.

`test_debug_settings.py` adds four checks interpreting actual startup/callback bodies:
AppSettings before host initialization and error propagation; toggling both ways while stored
values are stale; missing callback arguments/other settings; and closure applying saved runtime
values before the memory diagnostic/event. The old source fails ten subcases. AppSettings,
logging and initialization are serial mocks; native callback timing, persistence, plugin loading,
compilation and Vita cold-start/switch behavior remain maintainer validation.

Main background discovery replaces the former Search-specific lifecycle/list checks.
`test_discovery_lifecycle.py` interprets the actual persistent worker, activation/stop, generation
acceptance, locked value publication and aging with serial clock/kernel/socket mocks. It checks
five-second completion-to-send deadlines (including simulated send latency), one worker across
renewals/restarts, no catch-up, cancellation/stale observations, unchanged/value-copied snapshots,
expiry refresh, error-frozen age/log deduplication, lease timeout and final cleanup. Connect's
worker barrier and UI idle-poll wiring are also inspected.
The pause-crash follow-up changes worker control mocks to require pthread creation before
cancellable condition waiting, checks 64 KiB attributes and init/stack/create failure cleanup,
retry and final join, and interprets the Connect job's worker-only idle polling without pthread
condition waits on foreign PAF threads. The supplied core/local ELF establish the native failure
separately (UI guide); the serial model cannot reproduce Vita pthread TLS/ABI itself.
`test_host_lists.py` interprets actual Main eligibility, saved/discovered merging, identity,
row-state/token preparation, incremental cell-edit planner, deletion and captured selection/
canonicalization. It covers >8 rows, equal names, stronger conflicting identity/custom ports,
retained tokens/selection, stale replaced tokens, temporary-to-saved, failed reads/save, discovery
growth and no edits for unchanged rows. The linker follow-up also interprets the actual
PAF token-buffer mutation branches (front/middle/end insertion/deletion, move, singleton/empty)
and rejects reintroducing `std::vector` or sized PAF scratch constructors. These serial array
checks do not verify native allocation or successful linking. Two checkbox regressions interpret
actual row/Select All toggles, live-item updates and focus capture with token-only child lookup
after widget renaming, including 12 PCs and missing/recreated virtualized items. They reproduced
the old missing-checkmark/focus lookups and pass after correction; native PAF rendering and
input remain maintainer validation. XML checks compare saved/temporary columns and require
no temporary dot/Search resource. Return-to-Main routes and real installed PAF declarations are
source-inspected. `test_event_queue.py` also executes discovery snapshot coalescing and rejection
of stale/hidden-page delivery before subsequent connection events.
These mocks do not execute C++/CXML, real pthread/mutex scheduling, native PAF focus/scroll,
AppMgr suspension, sockets or Vita timing. Build/RCO and device acceptance remain pending.

The PC availability trial adds six checks in `test_host_availability.py` and one
in `test_event_queue.py`: actual TCP probe control flow with mocked native API
results/cleanup, cancellation and timeout; DNS handling; exact 30-second expiry and
UUID/address/port matching; failed submission preserving the previous drain barrier;
foreground-worker/shutdown ordering; availability snapshot coalescing and main-thread
delivery. The follow-up extends actual-predicate checks to the 15-second repeat
boundary, unfinished scans never starting a repeat, and pending replacement reads
preserving previous freshness without reading the worker's unfinished timestamp;
negative results and cancellation override that fallback. They do not execute
native threads, sockets, DNS, PAF, ABI or layout.

The earlier simplification through bcbf30d added sixteen checks beyond the 167-check remote-Quit stage:

- `test_ui_resources.py`: all resource XML is well-formed, local CXML style/template/texture
  references resolve, texture files exist, and inner `textobj` bindings are explicit. This
  parses XML; it does not compile an RCO or render PAF. The locale check reproduced unescaped
  ampersand/less-than attribute errors before correction, with decoded labels preserved.
- `test_network_lifecycle.py`: module/allocation/Net/NetCtl failure cleanup, successful and
  repeated initialization, shutdown order and idempotence, with native calls mocked.
- `test_application_cache.py`: invalidation, bounded reads, shorter refreshes, empty lists
  and rejected admission. It models copies in application elements, without C memory layout.
- `test_touch_events.py`: actual DS4/tablet contact handling for Down, stationary frames,
  movement to the origin, Up, ID reuse and out-of-range ID fallback. Coordinate sampling and
  transport are mocked. The old tablet path reproduced the stationary-Move failure.
- `test_mdns_bounds.py`: literal/compressed names and consumed offsets; invalid pointers,
  cycles, truncated labels and output capacity; valid Question/PTR/SRV/A and truncated record
  headers/data. Actual decoder/parser bodies are interpreted with byte arrays and mocked
  callbacks. No sockets, native memory/ABI or whole discovery lifecycle is executed.
- `test_video_scaling.py`: 30 fit/cover dimension cases covering zero/equal/narrow/wide
  sources, rounding/alignment and wide cross-products, including modeled integer casts.
  Decoder allocation, frame cropping and PAF presentation are outside the check.
- `test_video_crop.py`: actual crop/presentation fragment with byte pointers checks an
  exact 960×544 surface skips row copies, wider/taller surfaces keep exact crop pixels and
  untouched tail bytes, and dimensions/pitch/statistics remain correct. Six size/stats
  combinations model `memmove` and PAF callbacks; no GPU/decoder/native rendering is executed.
- `test_remote_quit.py`: one follow-up check preserves public null identity-path rejection
  before allocation after removing the worker's unused path copy. Existing launch/Quit
  checks continue to interpret the private helper with its shorter signature.
- `test_host_persistence.py`: an additional actual append/merge check covers 96 combinations
  of metadata, pairing, port/address preference and fill-only host/storage identity. Another
  drives the new-record copy tail into nonzero destination storage for lengths 0/1/254/255;
  the old source failed NUL termination at 255 for name/internal/external. File I/O/allocation
  remain mocked; this does not establish persistence durability or native memory behavior.
- `test_gamestream_worker.py`: actual worker admission/entry with serial pthread mocks;
  three nonzero errors at each init/stack/create stage reject work without invoking crypto,
  while successful creation joins and preserves three callback results. The old inline
  fallback failed all nine rejection cases. No real thread/stack/crypto execution occurs.

Network, cache, metadata-merge and video-scaling checks passed before and after their
refactors. These checks do not establish compilation, real threading, native UI, TLS/network
integration or Vita behavior; maintainer build/device validation remains separate.

The repeat audit adds `test_option_menu.py`: one actual-source check covers all
three native action callbacks, null userdata/callback, captured payloads and
delete-before-notify order (9 subcases), passing before and after consolidation.
C++ names/delete are normalized to serial mocks; PAF destruction/focus/native
transitions are not executed. The touch check additionally covers DS4 release,
tablet CANCEL_ALL and clearing contacts before/after sharing the dispatch helper.
The full gate passes 184 checks; compilation and Vita validation remain pending.

The pairing check interprets the actual verification helper with mocked EVP results (-1/0/1/2
at Init/Update/Final, 64 combinations); only success at all three stages may accept a proof.
`test_pairing_errors.py` adds ten source checks for typed local/OOM/security/PIN classification,
RNG/AES/RSA/certificate/parser/HTTP failure propagation, cleanup and final XML-response pin
rollback, server error payload/409, cancellation without a failure event and response-error reset.
The actual helpers and selected caller guards/API completion fragment are interpreted; pointer,
buffer, allocation, crypto, curl, XML and mutex behavior is modeled. This is not a complete
handshake or actual library/ABI/native-dialog/device validation. The recorded gate had 137 passes.
The server-trust checks interpret the actual X509 comparison expression with mocked comparison
results and check enrollment/rollback ordering. They do not execute OpenSSL, TLS or file I/O.
C grammar checks are not compilation or type checking.

`test_controller_light.py` adds ten non-build checks for controller RGB feedback and the two
optional stream overlays. It interprets the actual callback, accepted-start reset, snapshot getter
and normalized PAF update method: controller filtering, Off/stop/cancel/finished gating, fresh
black RGB on restart, black turning the light off, stale generation, unchanged-color deduplication,
missing widgets and no polling when disabled. It also checks capability/service wiring, update
without a new video frame, CXML layering/settings/localization and reproducible mirrored PNG masks.
Mutexes, pointer/struct copies, color vectors and widgets are serial mocks. These checks do not
compile CXML/C++, render PAF, run a host or verify real concurrency, Vita appearance/performance,
AppSettings persistence or actual game-generated RGB. See docs/UI.md section 4.10.

`test_host_persistence.py` interprets the actual metadata writer, three host-store mutations and
the successful-pairing completion fragment. Eight checks inject fopen failure, each of eight
fprintf failures (sticky ferror), buffered close failure and directory failure; verify a single
close of every opened file, unchanged INI fields, new/existing record status propagation and
pairing success despite failed metadata save. The three fprintf macros are expanded from their
source definitions; char arrays, struct pointers, directory calls and stdio are mocked. The
previous source fails 24 cases across six of these tests; that source gate had 108 passing tests.
No compiler/ABI, actual filesystem/flush, concurrency, native-dialog or Vita behavior is tested.
Atomic replacement, memory rollback and power-loss durability are outside this correction.

Host-store simplification (2026-10-23): the same interpreter follows the shared metadata,
append and save helpers. Three additional checks cover new/existing records, absent metadata,
default/custom ports, UUID replacement versus pairing's fill-only rule, storage-directory
selection, preserved paired/address-preference state, append failure and rejected input.
All 150 non-build checks pass. These remain serial source checks with mocked allocation,
directory and file operations; maintainer build/Vita persistence validation is pending.

`test_ps_button_state.py` interprets the shared PS reset and the existing tap-timing fragment.
Two checks cover clearing stale pending input without releasing the overlay guard/PS lock,
reset call sites, delayed single tap, double tap at the window boundary, and hold suppression.
Time and stream-stop calls are serial mocks; controller-bit masking is replaced by a no-op.
This does not execute the full input loop, ShellUtil, real concurrency or Vita Quick Actions.

`test_ime_cleanup.py` interprets Main's result/cleanup prefix across read errors, Cancel, Enter,
unknown buttons and present/absent poll registration. It requires one Term and conditional
Unregister before outcome handling, with both state flags cleared. The check passes before
and after the cleanup refactor. It mocks the dialog and call list, excludes address processing,
and does not validate native IME/module behavior or PAF. The combined source gate is 153 checks.

`test_remote_quit.py` adds 13 non-build checks for the confirmed conflict → Quit → launch flow.
It interprets service busy/completion/message/poll methods, the stream's pre-launch decision,
API conflict identity payload and native Quit helper. Cases include Yes/No, canceled/aborted
dialog results with stale Yes, GetResult/Init/Term failure and cleanup retry, immutable pending
confirmation, fresh/stale/absent/same-app IDs, local cancellation, pinned/paired admission,
`/cancel` request parameters, response freeing, parser/transport/refresh failure and a host app
still running after Quit. One adapter check covers forwarding the confirmed target unchanged.
Time, dialog/call-list, HTTP/XML, pointers and globals are serial mocks; the stream prefix stops
before key generation and actual launch. These do not establish native ABI, real scheduling,
TLS, Expat parsing, PAF appearance or host/Vita behavior. The cancel parser's actual C fixtures
cover success, malformed/missing/duplicate/false fields, DTD and server errors, but remain
uncompiled/unrun by the agent. The combined non-build gate passes all 167 checks.
The maintainer reports successful basic Vita testing on 2026-10-23; no per-case log was
supplied. This does not execute the C fixtures or close the fault/race validation gates.

**Historical host-list gate (superseded by Main checks above):**
`test_host_lists.py` interpreted the shared host-snapshot/identity helpers, actual Main/Search
refresh/token-lookup functions, Main Select/Clear All/count/delete and Search's canonical lookup
fragment. Nine checks model 40 saved/16 discovered hosts, discovery growth between count/copy,
shrink/empty/error preservation, conflicting UUID/MAC and port, selection following a moved UUID,
obsolete tokens, Select/Clear All and deletion past eight, and lookup after eight despite duplicate
names. Callback bindings are source-checked. PAF vectors, allocation, service reads, widgets and
action-bar exit are modeled; C++ template/member syntax is normalized for the serial interpreter.
That source gate had 117 passing checks. No compiler/ABI, allocator, real threading/snapshot consistency, PAF
scrolling/focus, native dialogs or Vita run is established. Backend discovery still holds at most 16.

Main startup crash follow-up (2026-10-22): the original host-list normalizer replaced the actual
PAF sized vector constructor with idealized allocation, hiding its uninitialized-capacity defect.
That translation is removed; the tests model the corrected empty-construction/resize path and
reject reintroducing the unsafe row count constructor. All 127 checks pass. Allocation remains
mocked: this is not a test of the installed C++ SDK or PAF malloc. Dump/disassembly evidence and
the required maintainer cold-start retest are in [MAIN_STARTUP_CRASH.md](../docs/MAIN_STARTUP_CRASH.md).

**Historical one-shot discovery gate (superseded by worker checks above):**
`test_discovery_lifecycle.py` interpreted the actual service Start/Stop, API bridge, native scan
worker/start/stop/shutdown and Search's immediate error branch. Nine checks model duplicate Start,
natural completion/restart, FINISHED before thread return, exact create/start errors with one
caller error, Stop before native worker entry/after completion, interrupted-scan cleanup and a
transient wait timeout. The previous native start code fails three cases while the current service/
API remain in place. All 126 non-build checks pass. C++ method calls are normalized; globals and
function-local scope, kernel/network calls and serial worker steps are mocked. No compiler/ABI,
real threads, PAF, sockets, blocked-I/O latency, permanent wait errors or Vita proof is provided.

The foreground checks interpret the source terminal-event predicate across all event/operation
combinations and the retained-result classification (including a successful empty app list).
They do not execute PAF dialogs, threads, event-ring delivery or the complete state machine;
dialog handoffs and overlap prevention still require maintainer build/Vita validation.

`test_event_queue.py` interprets the actual event_queue.c functions/header and the production
MoonlightApp::ProcessEventTask body. Twelve new checks cover 400 snapshot updates interleaved
with pairing/start/stop/close; ordered scan/error events; latest-snapshot merge order; owned
address/PIN/server-error payloads; producer updates while the head waits for worker completion;
ack-before-callback and no replay; ring reuse/reset; null/truncated addresses; Connect's existing
fallback; and explicit ordered-only overload without overwriting accepted events. The capacity
is 16 ordered entries plus two snapshot slots and one pending head; this does not promise delivery
for arbitrary critical-only storms. C struct assignment, pointer identity, fixed arrays, string
copies, sizeof and serial scheduling/mutex calls are modeled. C grammar parsing is not compilation,
type/ABI checking, real threading, PAF/native-dialog execution or hardware validation. Run the
maintainer's ordinary discovery/pair/Apps/launch/stop/restart smoke sequence after building.

The adapter lifecycle checks interpret actual Start/Stop/Disconnect and ForwardEvent method
bodies with mocked API calls. They exercise callbacks before and after API return, immediate
errors, no-host rejection and late Connect/Pair returns after closure. The initial Start/Stop
checks reproduced seven ordering failures before correction. These are serial ordering checks,
not C++ type checking, real-thread synchronization or PAF execution.

The disconnect-admission checks interpret the actual API gate, entry points, cleanup worker
and shutdown with mocked mutex/thread/stream functions. They cover contention, pending admission
rejection (including cached app reads), duplicate/idle disconnect, pending-stream stop before
GameStream release, creation/stop failure rollback and shutdown yielding to a queued worker.
The C++ scoped-request syntax is normalized to its actual helper; destruction/unlock and atomic
operations are simulated. They do not validate compiler/ABI/RAII behavior, pthread scheduling,
real synchronization, curl/crypto lifetime on hardware or end-to-end DisconnectHost behavior.

`gamestream_xml_test.c` exercises the actual Expat parser API: entities/UTF-8,
empty and bounded lists, malformed documents, invalid IDs, root errors, verb
results, and session URLs. These fixtures require compilation by the maintainer;
the agent has not compiled or executed them. Example desktop command for the maintainer:

    cc -std=c99 -Iinclude tests/gamestream_xml_test.c src/moonlight/gamestream_xml.c -lexpat -o /tmp/gamestream_xml_test
    /tmp/gamestream_xml_test

These checks do not establish Vita networking, pairing or stream compatibility.

The opt-in [Vita disconnect/reconnect scenarios](runtime/README.md) give the maintainer
separate idle, active-stream, post-launch/pre-RTSP, in-flight HTTPS and common-c startup
DisconnectHost triggers through existing UI selection. All five are excluded by default and
are mutually exclusive. The active scenario waits for a
user-selected stream, requires STOPPED before CLOSED, and restarts the same app only after
paired reconnect and fresh Apps. Its checks include timer/busy gating, no restart before
closure, queued STOPPED after the adapter already reports DISCONNECTED, wrong host/app/state,
operation rejection/failure, timeout, user interruption and no repeat after PASS. These Python
checks interpret the actual source with mocked services/time, not Vita runtime or compilation.


The pending scenario's source checks interpret both its worker checkpoint and main-thread
flow. Atomics, services, time and worker yields are mocked; serial interleavings cover disconnect
while Launch is busy, expected -7 before CLOSED, no reconnect under the native error/OK dialog,
no early STREAM_STARTED, one-shot recovery and release on failure/shutdown/worker timeout.
A separate check interprets the actual post-response/pre-RTSP cancel fragment: failed launch
responses bypass the hook, cancellation returns -7, and an uncancelled path continues. These
checks do not compile C++ or establish actual memory ordering, real-thread/curl lifetime,
Vita error presentation or recovery playback. See runtime/README.md for maintainer steps.


The HTTP mode reuses the pending controller checks with its own log prefix and checkpoint label.
Additional actual-source C checks exercise callback filtering/complete headers, response-before-
checkpoint rejection, internal-handle isolation, no trace-data output, one-shot availability,
curl option setup-failure cleanup and removal of stack userdata before unlock. Mocked pointer
arithmetic/struct writes are used; no real curl, TLS, memory ordering, thread scheduling or C/C++
ABI is tested. The existing C grammar check still covers all trust/transfer/pairing fragments.
See the in-flight HTTPS maintainer procedure before claiming device/lifetime acceptance.


The startup mode now requires interrupted setup → STREAM_FAILED=-7 → CLOSED → native error OK →
paired reconnect/fresh Apps/same-app STREAM_STARTED. It rejects a first STARTED/STOPPED and
reports late_start=0 on PASS. The previous late_start=1 Vita result remains historical evidence,
not validation of this interruption change. The stage-filtered one-shot worker gate and source
checks cover busy admission, wrong/duplicate events, request failure, user-paced error OK,
timeout/shutdown release, the guarded callback and all 32 five-mode Boolean combinations.
This is not CMake configure or compilation.

The existing startup mode also accepts checkpoint 10 after successful video startup, before
audio startup/STARTED. Source checks require a complete three-slot pool/nonzero generation
before cancellation and cleared metadata of the same generation before reconnect. They cover
partial/absent pools, changed generation, bounded release wait and one-shot recovery. The test-only
observer's actual C body is interpreted over all 512 combinations of slot memblock/pointer/mapping
presence (loop increment/declaration normalized for the interpreter). Real allocator, native
unmap/free results, races and decoder/GPU lifetime are not established by these serial checks.
Stage 4 remains default; the stage-10 maintainer command and acceptance sequence are in
[runtime/README.md](runtime/README.md#after-video-allocation-checkpoint).

The production startup-interrupt checks interpret the actual mutex-protected publication,
stop, connection-ready and finish helpers. They cover cancellation before/after common-c's
reset, repeated stop/stage publication, success racing an interrupt, STARTED commit before stop,
uncancelled failures and validation errors before the first callback. They require no input/
STARTED after a winning cancel, owner-only stop exactly once for cancelled success, no second
stop after common-c's failed-init unwind, and owner reservation through terminal bookkeeping.
An additional actual-source check drives Apps' failure handler into the existing deferred-release
path, then exercises its three-tick delay, stale generation and active-Stream rejection guards.
All mutex/interrupt calls, atomics, services and time are mocked/serial. No real threads,
common-c/RTSP/TLS, PAF surfaces, compiler/ABI or hardware behavior is validated.
