# Focused checks

Non-build checks (Python 3 and pycparser):

    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py'

The pairing check interprets the actual verification helper with mocked EVP results (-1/0/1/2
at Init/Update/Final, 64 combinations); only success at all three stages may accept a proof.
`test_pairing_errors.py` adds ten source checks for typed local/OOM/security/PIN classification,
RNG/AES/RSA/certificate/parser/HTTP failure propagation, cleanup and final XML-response pin
rollback, server error payload/409, cancellation without a failure event and response-error reset.
The actual helpers and selected caller guards/API completion fragment are interpreted; pointer,
buffer, allocation, crypto, curl, XML and mutex behavior is modeled. This is not a complete
handshake or actual library/ABI/native-dialog/device validation. All 137 Python checks pass.
The server-trust checks interpret the actual X509 comparison expression with mocked comparison
results and check enrollment/rollback ordering. They do not execute OpenSSL, TLS or file I/O.
C grammar checks are not compilation or type checking.

`test_host_persistence.py` interprets the actual metadata writer, three host-store mutations and
the successful-pairing completion fragment. Eight checks inject fopen failure, each of eight
fprintf failures (sticky ferror), buffered close failure and directory failure; verify a single
close of every opened file, unchanged INI fields, new/existing record status propagation and
pairing success despite failed metadata save. The three fprintf macros are expanded from their
source definitions; char arrays, struct pointers, directory calls and stdio are mocked. The
previous source fails 24 cases across six of these tests; all 108 current Python tests pass.
No compiler/ABI, actual filesystem/flush, concurrency, native-dialog or Vita behavior is tested.
Atomic replacement, memory rollback and power-loss durability are outside this correction.

`test_host_lists.py` interprets the shared host-snapshot/identity helpers, actual Main/Search
refresh/token-lookup functions, Main Select/Clear All/count/delete and Search's canonical lookup
fragment. Nine checks model 40 saved/16 discovered hosts, discovery growth between count/copy,
shrink/empty/error preservation, conflicting UUID/MAC and port, selection following a moved UUID,
obsolete tokens, Select/Clear All and deletion past eight, and lookup after eight despite duplicate
names. Callback bindings are source-checked. PAF vectors, allocation, service reads, widgets and
action-bar exit are modeled; C++ template/member syntax is normalized for the serial interpreter.
All 117 Python checks pass. No compiler/ABI, allocator, real threading/snapshot consistency, PAF
scrolling/focus, native dialogs or Vita run is established. Backend discovery still holds at most 16.

Main startup crash follow-up (2026-10-22): the original host-list normalizer replaced the actual
PAF sized vector constructor with idealized allocation, hiding its uninitialized-capacity defect.
That translation is removed; the tests model the corrected empty-construction/resize path and
reject reintroducing the unsafe row count constructor. All 127 checks pass. Allocation remains
mocked: this is not a test of the installed C++ SDK or PAF malloc. Dump/disassembly evidence and
the required maintainer cold-start retest are in [MAIN_STARTUP_CRASH.md](../docs/MAIN_STARTUP_CRASH.md).

`test_discovery_lifecycle.py` interprets the actual service Start/Stop, API bridge, native scan
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
