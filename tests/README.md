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
