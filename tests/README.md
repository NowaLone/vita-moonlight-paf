# Focused checks

Non-build checks (Python 3 and pycparser):

    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py'

The pairing check interprets the actual verification expression with mocked EVP results.
The server-trust checks interpret the actual X509 comparison expression with mocked comparison
results and check enrollment/rollback ordering. They do not execute OpenSSL, TLS or file I/O.
C grammar checks are not compilation or type checking.

The foreground checks interpret the source terminal-event predicate across all event/operation
combinations and the retained-result classification (including a successful empty app list).
They do not execute PAF dialogs, threads, event-ring delivery or the complete state machine;
dialog handoffs and overlap prevention still require maintainer build/Vita validation.

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
