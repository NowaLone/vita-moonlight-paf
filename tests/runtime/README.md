# Vita DisconnectHost smoke tests

## Idle scenario

This opt-in package performs one local **idle** disconnect and reconnect to the PC selected
through the existing UI. It uses ConnectionService/ApplicationService and MoonlightApp's
main-thread event delivery. It adds no button, endpoint, host credentials or persistent setting.
It does not launch/quit a game or revoke pairing. Pending-launch/active-stream disconnect,
duplicate/admission races, failure rollback and shutdown remain separate tests.

### Maintainer steps

1. Build the test system-profile package from the repository root:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_DISCONNECT_TEST=ON
   ```

   The agent must not execute this command. The usual system profile defaults apply;
   preserve any budget arguments you normally need. Install `build/vita_moonlight_paf.vpk`
   using your usual transfer/installation workflow.

2. Launch Moonlight and open Settings. Enable **Debug log**, then return to the PC list.
   Opening Settings initializes the logging preference for this process; no console/file
   logging is forced by the test.
3. Select the PC normally, completing PIN pairing if necessary. When Apps loads, leave the
   controls untouched and do not start a game. The test automatically disconnects, waits for
   CLOSED, reconnects to the same host/port with saved trust and reloads Apps.
4. Copy the `[DisconnectTest]` lines through PASS or FAIL from `psp2shell`. Expected sequence:

   ```text
   [DisconnectTest] armed host=... port=...
   [DisconnectTest] begin idle disconnect/reconnect; leave controls untouched
   [DisconnectTest] disconnect request result=0
   [DisconnectTest] CLOSED observed state=DISCONNECTED
   [DisconnectTest] reconnect enqueue result=0
   [DisconnectTest] reconnect ready state=PAIRED
   [DisconnectTest] refresh enqueue result=0
   [DisconnectTest] PASS idle CLOSED -> reconnect -> applications count=...
   ```

   The count can be zero. If another page legitimately initiates the post-reconnect refresh,
   its ready event may finish the test without the test's own refresh-enqueue line. A service
   error, mismatched host/state, unexpected PIN/stream event or 60-second step timeout reports
   FAIL and stops further automatic test actions. Existing native error dialogs remain usable.
   A timeout is an incomplete test, not proof that the backend stopped; normal cleanup still
   owns outstanding work. Close/relaunch the application to repeat the one-shot test.

5. Restore the ordinary package afterward:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON
   ```

   `build.sh` explicitly defaults all test modes OFF on each invocation. Direct CMake users
   must pass `-DMOONLIGHT_DISCONNECT_TEST=OFF`, `-DMOONLIGHT_ACTIVE_DISCONNECT_TEST=OFF` and
   `-DMOONLIGHT_PENDING_DISCONNECT_TEST=OFF` to clear previously enabled cache entries.

### Scope and implementation

The option defaults OFF. Its source and main-thread hooks are compiled only with the option ON.
The original selected MoonlightHost is captured by ConnectionService after foreground reservation;
the hook includes custom port/identity data instead of guessing the PC from list order. The test
observes events after PrepareCompletion and runs commands on the next PAF poll. Connect and app
refresh still use their existing jobs/native busy dialogs; Disconnect uses its existing cleanup
worker. The scenario replaces no frontend callback and performs no UI work on a worker thread.
Changing PCs during the active test aborts it. Ordinary builds contain no test logs or actions.

PASS establishes the observed idle CLOSED → paired reconnect → authenticated application-list
sequence only. It does not establish absence of leaks, exact animation timing, streamed teardown,
hardware input behavior, API thread safety or the full F5 matrix.

The maintainer supplied a successful Vita log on 2026-10-21: disconnect result=0,
CLOSED/DISCONNECTED, same-target reconnect/PAIRED without PIN, a fresh four-app list and PASS.
The following Apps callback logs `before=4 after_delete=0 after_insert=4`. This verifies one
normal idle cycle and nonempty list rebuild; repeat/failure/pending/active/shutdown cases remain
open. The fragment has no launch revision/profile or Sunshine version marker. See
[API audit F5](../../docs/SUNSHINE_API_AUDIT.md#f5--sequential-foreground-workflow-operation-state-and-lifecycle-coverage)
for evidence and limits. No build/device test was run by the agent. Python source-interpretation
checks mock service calls and time; they do not compile C++ or execute PAF/network operations.

## Active-stream scenario

This is a separate package mode. Select and start an app yourself; the scenario captures its
actual STREAM_STARTED app ID and waits three seconds without blocking PAF. It then calls
ConnectionService::Disconnect, requires a successful STREAM_STOPPED before CLOSED/DISCONNECTED,
reconnects to the captured host/port without pairing, refreshes Apps and requests Start for the
same app ID once. PASS requires the second STREAM_STARTED with that ID and STREAMING state,
not merely a successful return from Start. Normal frontend callbacks still close/reopen the
Stream page and schedule deferred frame-buffer release. The resumed stream remains active;
there is no second automatic disconnect.

Maintainer procedure:

1. Build/install the test VPK (the agent must not execute the command):

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_ACTIVE_DISCONNECT_TEST=ON
   ```

   Install `build/vita_moonlight_paf.vpk`. Enable only one of idle, active or pending setup;
   CMake rejects combined test modes. build.sh resets all three OFF before explicit arguments,
   including when replacing an earlier test cache.
2. Open Settings, enable Debug log, return and connect to the PC normally. Start one application
   from Apps. Keep the PC app running and leave Vita controls untouched during the automatic
   disconnect/reconnect/restart. No app is automatically chosen from the list. Initial pairing
   is allowed; pairing requested after disconnect fails the test.
3. Capture the log from `[Launch]` through the final `[DisconnectActiveTest] PASS` or FAIL:

   ```text
   [DisconnectActiveTest] armed host=... port=...
   [DisconnectActiveTest] ready; start one app, then leave controls untouched
   [DisconnectActiveTest] active app=...; disconnect in 3 seconds
   [DisconnectActiveTest] disconnect request result=0
   [DisconnectActiveTest] STOPPED observed app=...
   [DisconnectActiveTest] CLOSED observed state=DISCONNECTED after STOPPED
   [DisconnectActiveTest] reconnect enqueue result=0
   [DisconnectActiveTest] reconnect ready state=PAIRED
   [DisconnectActiveTest] refresh enqueue result=0
   [DisconnectActiveTest] refreshed applications count=...
   [DisconnectActiveTest] restart request app=... result=0
   [DisconnectActiveTest] PASS active STOPPED -> CLOSED -> reconnect -> stream started app=...
   ```

   Include the normal `[Stream]`, `[Video]` and GameStream lines between markers: owner stop
   completion before CLOSED, deferred framebuffer unmaps, and the second launch/resume choice
   and RTSP setup. A legitimate page-initiated refresh can omit the test refresh-enqueue line.
4. Confirm that image and sound return after reconnect and the host app remains the selected one.
   PASS alone confirms event/state ordering, not decoded/presented video or audible playback.
   Verify `[GameStream] resume app=...` to claim resume; fresh serverinfo may legitimately choose
   launch if the host app exited. Do not report that alternative as a verified resume.
5. Stop normally after observing the resumed stream, then restore the ordinary package with
   `./build.sh -DMOONLIGHT_SYSTEM_APP=ON`.

The active implementation is compiled only for this option, using the existing test hook guard;
no production protocol, lifecycle ownership, UI callbacks or storage format is changed. The
three-second hold begins at connection establishment and does not prove video has arrived.
Each automatic step has a 60-second deadline; waiting for the user's first stream has no test
deadline. Rejected operations, wrong host/app/state, failed stream, early/duplicate stop, CLOSED
without STOPPED, unexpected pairing, another Connect or shutdown stop further test actions.
FAIL/timeout does not forcibly cancel outstanding backend work: normal cleanup remains its owner.
Relaunch to repeat the one-shot scenario. Controls must remain untouched; the test does not
intercept input or prevent ordinary Stop/Back actions from aborting its expected sequence.

The maintainer supplied a passing Vita log on 2026-10-21, identifying system build
f25e735baf54: owner stop result=0 → STOPPED → CLOSED/DISCONNECTED → paired reconnect without PIN
→ fresh four-app list → same-app resume/RTSP → PASS. The resumed stream decodes 654 frames and
receives audio; final double-PS stop completes cleanup with result=0. Both teardowns log three
successful framebuffer unmaps. The maintainer subsequently confirms for this exact run
(2026-10-21): “Да, изображение и звук работают” after reconnect. This verifies one normal
cycle with user-confirmed visible/audible playback; no agent-run build/device test or host
resource trace.
See [API audit F5](../../docs/SUNSHINE_API_AUDIT.md#f5--sequential-foreground-workflow-operation-state-and-lifecycle-coverage)
for evidence and limits. Python checks interpret the actual scenario
with mocked services/time and queued event ordering. They do not compile C++ or execute PAF,
real threads or transport; broader device coverage beyond the reported cycle remains open.
Pending-launch cancellation, duplicate admission, failure rollback, shutdown and stress coverage
remain separate. Existing audit references C/QS/V/S distinguish local stop from host-wide quit;
this test sends no `/cancel`, quit or unpair request and uses no Sunshine administrative API.


## Pending-setup scenario

This mode targets one precise A16 checkpoint: **after a successful HTTPS launch/resume response,
before RTSP/LiStartConnection**. It does not test cancellation while curl is waiting, inside RTSP
or during common-c startup. A failed launch/resume response never enters the checkpoint.

The user chooses an app in Apps for the first attempt. In this test package only, its worker publishes
an atomic checkpoint and waits for release, holding no stream/control mutex. PAF's test poll
requests the real service Disconnect while Launch is still busy, then releases the worker.
The existing stream cancel check must return -7 before any first STREAM_STARTED. The scenario
requires STREAM_FAILED=-7 for that host/app followed by CLOSED/DISCONNECTED; it then waits for
the user to dismiss the ordinary native launch error. It reconnects, requires PAIRED without PIN,
refreshes Apps and starts the same app once without holding the checkpoint again. PASS requires
the recovery STREAM_STARTED/STREAMING with the same host/app, not accepted Start alone.

Maintainer procedure (agent must not build):

1. Build and install the pending-test package:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_PENDING_DISCONNECT_TEST=ON
   ```

   Install `build/vita_moonlight_paf.vpk`. Other disconnect modes must remain OFF; build.sh
   clears their cached ON values unless explicitly requested. Normal budgets/settings apply.
2. Open Settings, enable Debug log, return, connect and start one app from Apps. The first
   attempt should be cancelled before video. Initial pairing is allowed if needed.
3. Expect one ordinary **Unable to start stream** error (`0xFFFFFFF9`, -7). Press OK once.
   This is the intended cancellation result delivered through the existing frontend callback;
   it is not hidden or automatically dismissed by the test. Do not select/start another app.
   The test waits for OK without a test deadline, then reconnects and restarts automatically.
4. Capture from `[Launch]` through `[DisconnectPendingTest] PASS` or FAIL, including normal
   GameStream/Stream setup lines. Expected markers:

   ```text
   [DisconnectPendingTest] armed host=... port=...
   [DisconnectPendingTest] ready; start one app; expect one cancellation error/OK
   [DisconnectPendingTest] start accepted app=...; awaiting checkpoint
   [DisconnectPendingTest] checkpoint after launch response before RTSP app=...
   [DisconnectPendingTest] pending disconnect request result=0
   [Stream] launch cancelled before RTSP setup
   [Stream] setup finished app=... result=-7
   [DisconnectPendingTest] cancelled setup observed result=-7 app=...
   [DisconnectPendingTest] CLOSED after cancelled setup; dismiss launch error with OK
   [DisconnectPendingTest] reconnect enqueue result=0
   [DisconnectPendingTest] reconnect ready state=PAIRED
   [DisconnectPendingTest] refresh enqueue result=0
   [DisconnectPendingTest] refreshed applications count=...
   [DisconnectPendingTest] restart request app=... result=0
   [DisconnectPendingTest] PASS pending cancelled -> CLOSED -> reconnect -> stream started app=...
   ```

   Worker/main-thread messages can interleave: the checkpoint can log before Start returns.
   The old attempt must have no RTSP/connection-start message after its accepted launch response.
   A page-initiated fresh app refresh can omit the test's own refresh-enqueue line. Inspect the
   recovery launch/resume choice and RTSP stages; only the recovery should reach STREAM_STARTED.
5. Confirm image/sound and selected host/app after recovery, stop normally and capture cleanup.
   PASS alone is an event/state check. A pending host launch may leave the app running; fresh
   serverinfo decides resume versus launch. Host pending-session rejection/failure is recorded
   as FAIL with the actual backend result, without timed retries, `/cancel` or a Sunshine fix.
6. Restore the ordinary package with `./build.sh -DMOONLIGHT_SYSTEM_APP=ON` afterward.

Only the pending option compiles the new worker/main-thread hooks and scenario source. The
worker checkpoint is one-shot and uses only atomic publication, time and a 10 ms worker yield;
it has a 60-second safety timeout if PAF stops responding. Main-thread failure/shutdown releases
it too. The worker makes no PAF calls, and the main thread never waits on this checkpoint;
only the stream worker is held.
Automatic backend steps have 60-second test deadlines; first app selection and cancellation OK
are user-paced. Timeout/error releases the barrier and stops further test commands. Normal
backend cleanup still owns outstanding work; releasing a failed test does not itself cancel
setup. No production cancellation flag, API semantics, host identity/storage or page callback is
changed, and no forced remote quit/unpair request is sent. Relaunch to repeat the one-shot test.

Non-build checks interpret the actual checkpoint/main state flow with mocked atomics/services,
time and serial interleavings, plus the real post-response cancel-check fragment. They cover
busy-launch disconnect, -7/CLOSED ordering, wrong app/host/state/result, no early STARTED, wait
for error dismissal, request failures, worker/main timeout, shutdown release and a single
same-app recovery. They do not compile C++, validate atomic memory ordering or real scheduling,
run curl/RTSP or prove absence of use-after-free. Remaining acceptance is scoped by the failed
immediate-recovery and successful delayed-OK Vita results below. Other A16 stages, duplicate
requests, rollback, shutdown/stress and broader T7/T8 coverage remain separate.

### Maintainer result — 2026-10-21: FAIL during recovery RTSP

The supplied Vita log verifies the checkpoint, real disconnect, -7 setup cancellation before
RTSP, CLOSED, native error dismissal, paired reconnect and four-app reload/rebuild. The second
Start was requested and its resume was accepted. RTSP OPTIONS then fails with
`Failed to decrypt RTSP response`, stage 4 error=-1, setup result=-1 and test FAIL/AwaitRestarted.
No second STREAM_STARTED or PASS occurs. The maintainer reports Sunshine 2025.924.154138 and
its `Failed to verify RTSP message tag` at 12:57:23.132. The Vita fragment has no Launch marker.

Versioned source retains an older pending session when another is raised; a fresh client key
against that retained session explains the two authentication failures, but exact session/key
selection is inferred, not traced. See [API audit F5](../../docs/SUNSHINE_API_AUDIT.md#f5--sequential-foreground-workflow-operation-state-and-lifecycle-coverage)
for upstream links and limits. Keep this as a failed immediate-recovery test. It is not an
error-dialog wait failure; the reconnect/restart markers are present. No automatic retry,
forced cancel, reused key or encryption downgrade is added. Expiry followed by a new explicit
attempt remains unverified; no agent build/device test or runtime change accompanies this record.


To distinguish pending-session expiry from another RTSP failure, keep this Moonlight process
open in Apps after FAIL and leave the PC app running. With unchanged Sunshine ping_timeout
(default [10 seconds in this release](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/config.cpp#L479-L480)),
wait at least 15 seconds after FAIL and select the same app once. With a custom timeout, wait
longer than its configured value. Do not relaunch Moonlight for this check: this one-shot test
is already Finished, so the explicit new start bypasses its checkpoint. Capture resume/RTSP
and playback/stop logs. This is an unrun diagnostic follow-up, not a client retry workaround.

### Maintainer follow-up — 2026-10-21: delayed error OK succeeds on a second host

The maintainer identifies Sunshine 2026.914.233613, describes it as the latest version and
reports analogous immediate-retry behavior. For the supplied PASS run they waited roughly
five seconds before pressing the cancellation error's OK. The log uses
host .154, another server UUID and app=2072647516, with no Launch revision marker.

The trace verifies -7 before RTSP → CLOSED → error dismissal → paired reconnect without PIN
→ fresh one-app list/rebuild → same-app resume → successful RTSP/setup → PASS. Audio packets
arrive and 404 frames decode; final double-PS stop completes owner cleanup and three framebuffer
unmaps with result=0. The maintainer reports successful restart; visible/audible playback is not
separately itemized. Video recovery events occur before continued decoding, so sustained stream
quality is not established by this test.

The pause is approximate and has no timestamped measurement. Other operations add elapsed time;
host/version also differ from the preceding failing trace. This supports the pending-expiry
explanation but is not a controlled timing-only experiment or a universal five-second rule.
It is delayed OK before automatic recovery, not the proposed explicit retry after a finished
FAIL. Preserve both results; immediate recovery and other A16/F5 cases remain open. No fixed
wait/retry or runtime-code change is introduced. See API audit F5 for the complete evidence.
