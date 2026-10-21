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
   must pass `-DMOONLIGHT_DISCONNECT_TEST=OFF`, `-DMOONLIGHT_ACTIVE_DISCONNECT_TEST=OFF`,
   `-DMOONLIGHT_PENDING_DISCONNECT_TEST=OFF`, `-DMOONLIGHT_HTTP_DISCONNECT_TEST=OFF` and
   `-DMOONLIGHT_STARTUP_DISCONNECT_TEST=OFF` to clear previously enabled cache entries.

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

   Install `build/vita_moonlight_paf.vpk`. Enable only one of idle, active, pending setup, HTTP or startup;
   CMake rejects combined test modes. build.sh resets all five OFF before explicit arguments,
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


## In-flight HTTPS scenario

This fourth mode reuses the pending test's main-thread state machine and one-shot barrier with
`[DisconnectHttpTest]` markers. Its checkpoint is inside curl_easy_perform for the selected
HTTPS launch/resume, at outgoing GET headers ending in CRLFCRLF, before response headers are
processed. The former post-response hook is excluded in this mode. This exercises a live curl
handle/response buffer and the HTTP mutex during Disconnect, unlike the earlier completed-HTTP
checkpoint. It does not require a slow host, inject network failures or fabricate a response.

Maintainer procedure (the agent must not build):

1. Build/install:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_HTTP_DISCONNECT_TEST=ON
   ```

   Install `build/vita_moonlight_paf.vpk`. Do not explicitly enable another disconnect option;
   all five default OFF per script invocation. No new persistent setting is added.
2. Open Settings, enable Debug log, return, connect and start one app. Leave other controls
   untouched during the automatic steps except the expected cancellation error's OK.
3. Capture `[Launch]` and all logs through PASS or FAIL. The critical lifetime ordering is:

   ```text
   [DisconnectHttpTest] request headers sent; curl active before response app=...
   [DisconnectHttpTest] checkpoint during curl before response app=...
   [DisconnectHttpTest] pending disconnect request result=0
   [DisconnectHttpTest] response headers received
   [DisconnectHttpTest] transfer end result=0 checkpoint=1 response_headers=1
   [Stream] launch/resume accepted session=...
   [Stream] launch cancelled before RTSP setup
   [Stream] setup finished app=... result=-7
   [DisconnectHttpTest] cancelled setup observed result=-7 app=...
   [DisconnectHttpTest] CLOSED after cancelled setup; dismiss launch error with OK
   ```

   Start-acceptance messages may interleave with the worker. In the normal targeted transfer,
   response/transfer-end must follow the disconnect request and precede failed setup/CLOSED.
   A transfer error/timeout remains its actual backend error and does not count as this success.
   A missing checkpoint, non-target request, mismatched host/app/state or first STREAM_STARTED
   cannot produce PASS. Copy the actual log if the callback is not reached; do not move the
   hook to after the response and claim in-flight coverage.
4. The ordinary **Unable to start stream (0xFFFFFFF9)** dialog is the expected -7 result.
   Dismiss it once. The test waits for OK, then performs paired reconnect, a fresh app list and
   Start for the same app. PASS is:

   ```text
   [DisconnectHttpTest] PASS pending cancelled -> CLOSED -> reconnect -> stream started app=...
   ```

   Confirm image/sound and normal final stop. As in the earlier pending test, immediate
   recovery can separately fail at RTSP while Sunshine retains an older pending session.
   Preserve that FAIL; the successful -7/CLOSED part alone is not full recovery acceptance.
   Record any user pause before OK; no wait/retry is injected by the client. A human delay
   must not be presented as a universal required/sufficient five-second interval.
5. Restore the ordinary package with `./build.sh -DMOONLIGHT_SYSTEM_APP=ON` afterward.

The test installs a private curl debug sink only for the first armed HTTPS launch/resume.
It inspects the fixed request-line prefix and header terminator only: no curl trace data,
URL/query, certificate or session key is printed/copied. Internal curl handles and other verbs
cannot trigger the checkpoint. Debug verbosity is enabled only after the sink/data are installed
and disabled before clearing them, including setup failure. Cleanup occurs before releasing
http_request's HTTP mutex and before its stack context expires. Existing pairing progress,
TLS pinning, HTTP timeouts and write callbacks are unchanged.

Official [curl debug API](https://curl.se/libcurl/c/CURLOPT_DEBUGFUNCTION.html) describes HEADER_OUT
as outgoing header data; [curl 8.17.0 request.c](https://github.com/curl/curl/blob/curl-8_17_0/lib/request.c#L201-L208)
issues it after the send call. Installed VitaSDK headers report 8.17.0; the maintainer trace below
now confirms the targeted callback sequence on one Vita, without identifying the linked library
revision or establishing all callback fragmentation cases. The checkpoint holds
the HTTP transfer mutex, but no stream/API control mutex; main-thread Disconnect only requests
stop and queues cleanup. The owner retains curl/response until perform returns. The barrier is
released by the request, failure/shutdown or its 60-second worker deadline. The existing stream
cancel flag is checked after HTTP; this mode does not abort curl or add cancellable HTTP support.
Thus the response may already be buffered at the peer/OS while the callback is held. It tests
client-side in-flight lifetime, not a proven server processing delay or TCP retransmission wait.
Unrecognized fragmented request-line/terminator callbacks can miss the checkpoint; that is an
incomplete test, not cancellation coverage.

### Maintainer result, 2026-10-21

The supplied `[DisconnectHttpTest]` trace for host .134/app 1093255277 passes the targeted
sequence: outgoing-header checkpoint → Disconnect=0 → response headers → transfer result=0
(checkpoint=1, response_headers=1) → accepted resume → setup -7 before RTSP → CLOSED → error
termination → reconnect/PAIRED without PIN → fresh four Apps/rebuild → same-app encrypted
RTSP/setup=0 → PASS. Video decodes through sampled frame 661 (number 665); first video packet
is logged at 400 ms, first audio packet at 2700 ms. The HTTP callback is one-shot: recovery
proceeds without entering a second checkpoint.

The log ends during streaming. The maintainer subsequently confirms visible image, audible
sound and ordinary double-PS stop (user report). Final STOPPED/release/unmaps are not captured.
Current Sunshine version, installed client revision/profile (no Launch marker) and error-OK
timing are unspecified. Do not infer them from earlier host
reports or claim a fix for the earlier immediate-recovery RTSP tag failure. This is one successful
client-side in-flight ownership/recovery run; HTTP errors/timeouts, RTSP/common-c cancellation,
shutdown, repeated recovery and leaks remain separate. No runtime-code change or agent build/
device run accompanies this record. Restore the ordinary package as described above. Next
prepare a separate RTSP/common-c startup cancellation scenario. Full evidence and limits: API audit F5.


## Common-c startup scenario

**Current interrupt follow-up, 2026-10-21; stage-4 Vita result recorded below:** use
`MOONLIGHT_STARTUP_DISCONNECT_TEST=ON` (default checkpoint stage 4) to hold the sole worker in
stageStarting(STAGE_RTSP_HANDSHAKE), inside LiStartConnection after platform/name/audio init,
before handshake I/O begins. The checkpoint is unchanged; the production stop path now requests
LiInterruptConnection and the test expects no first STARTED. The older late_start=1 result below
predates this change and must not be counted as its hardware validation.

The first stage callback publishes interrupt-safe entry after common-c resets its interrupt flag,
replaying any earlier local stop. Stop sends one interrupt only while startup is published and
STARTED has not committed. The owner closes this scope after LiStartConnection returns. If an
issued interrupt wins before STARTED, common-c's actual result is logged and the local result is
-7 after unwind. If common-c nevertheless returns success, input/STARTED is suppressed and the
same worker calls LiStopConnection before reporting -7. A stop after the STARTED commit uses
ordinary active teardown. Uncancelled failures and validation failures before first callback keep
their actual error. This is cooperative interruption; blocked socket/platform calls may still
wait for existing timeouts. The checkpoint does not test latency inside a blocked RTSP read.

Maintainer procedure (the agent must not build):

1. Build/install:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_STARTUP_DISCONNECT_TEST=ON
   ```

   Install `build/vita_moonlight_paf.vpk`. Only this mode should be enabled; all five options
   default OFF per build.sh invocation and CMake rejects combinations. No new setting exists.
2. Enable Debug log, connect normally and start one app. Leave other Vita controls untouched
   until the expected cancellation error appears. The first Stream page must not open for the
   targeted stage-4 cancellation. Expected order includes:

   ```text
   [Stream] stage start 4 (RTSP handshake)
   [DisconnectStartupTest] checkpoint inside LiStartConnection stage=4 before RTSP handshake
   [DisconnectStartupTest] startup disconnect request result=0
   [Stream] stage failed 4 (RTSP handshake) err=...
   ... common-c partial audio/platform cleanup ...
   [Stream] common-c startup cancelled result=...; owner cleanup returned
   [Stream] setup finished app=... result=-7
   [DisconnectStartupTest] cancelled setup observed result=-7 app=...
   [DisconnectStartupTest] CLOSED after cancelled startup; dismiss launch error with OK
   ```

   The first STREAM_STARTED/STREAM_STOPPED is a test failure. A timeout/other backend error
   cannot substitute for -7. On this checkpoint video/hardware audio startup has not occurred,
   so no first-session video unmaps or hardware audio-port release should be required to
   claim partial-init unwind. A later-stage cancellation can own those resources; Apps now
   schedules existing generation-guarded deferred release for STREAM_FAILED too.
3. Dismiss **Unable to start stream (0xFFFFFFF9)** with OK once. This remains the ordinary
   native error flow. The test waits for acknowledgement, then reconnects PAIRED without PIN,
   refreshes Apps and starts the same app. PASS now requires:

   ```text
   [DisconnectStartupTest] PASS startup cancelled -> CLOSED -> reconnect -> stream started app=... late_start=0
   ```

   No automatic delay/retry or remote quit is added. The earlier Sunshine pending-session
   immediate-recovery RTSP failure can remain a separate FAIL; preserve it and the successful
   -7/CLOSED portion. Record any human pause before OK, not a universal five-second rule.
4. Capture `[Launch]` and the complete log through PASS/FAIL. After PASS confirm image/sound,
   stop with double PS and include the cleanup tail (audio release/three video unmaps). Include
   the actual Sunshine version. If FAIL occurs, dismiss any native error normally; no further
   automatic actions are issued. Normal cleanup still owns any outstanding operation.
5. Restore the ordinary package afterward:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON
   ```

The test does not inject interrupt/stop itself or replace frontend callbacks. Its bounded stage
barrier runs without PAF or stream/control/HTTP mutex ownership and releases on request/failure/
shutdown/worker deadline (60 seconds). The production stop only sends a nonblocking interrupt
under the short stream mutex; LiStopConnection stays on the stream owner. Host cleanup waits
for worker completion and terminal-event bookkeeping. Apps uses the existing deferred release
with pool-generation and active-Stream guards. No vendored protocol, endpoint, credentials,
HTTP cancellation or Sunshine behavior is changed. The narrow stage-4 runtime result is below;
remaining boundary/race/latency cases are unverified.

References: [common-c asynchronous interrupt contract](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Limelight.h),
[reset/stage/unwind implementation](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Connection.c)
and [Moonlight Qt interrupt/serialized cleanup](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/session.cpp).
The existing stage callback provides the entry boundary without modifying common-c.

### Maintainer startup baseline result, 2026-10-21 (before interrupt follow-up)

The supplied log for host .134/app 1093255277 verifies stage-4 checkpoint → Disconnect=0 →
late STARTED/setup=0 → session-owner stop result=0 → STOPPED → CLOSED/DISCONNECTED. First-session
audio drain/release and all three deferred framebuffer unmaps succeed. Reconnect is enqueued
before deferred unmaps; these finish before recovery setup. CLOSED is host/transport completion,
not a claim that every PAF surface has already been freed.

Paired reconnect needs no PIN; fresh four Apps rebuild correctly. Same-app resume/encrypted RTSP/
setup=0 reaches PASS late_start=1. First video arrives at 300 ms; decode resumes after the logged
IDR wait/drop-limit/request sequence and continues through sampled frame 301 (number 424).
The no-audio/video warnings and ENet interrupt/control-failed:4 belong to the immediately
stopped first session, whose owner cleanup succeeds. Recovery audio is initialized but no
received-audio marker or final manual stop appears in this fragment. The maintainer subsequently
confirms visible image, audible sound and ordinary double-PS stop (user report).

Source mode was prepared in 95eb968; no Launch marker/current Sunshine version identifies this
installed binary/host. USER_RW free is 33880 KiB initially and 33600 at paired reconnect; one
cycle neither diagnoses a leak nor proves leak freedom. This closes the targeted late-start
teardown/recovery test through PASS/decoded video only. Prompt interruption, failed startup
rollback, blocked RTSP I/O, timeout/shutdown/repeats and logged final recovery resource release
remain separate. No runtime-code change or agent build/device run accompanies this record.


### Maintainer interrupted-startup result, 2026-10-21

The strict test now passes on the supplied Vita log for host .134/app 1093255277:
stage-4 checkpoint/Disconnect=0 → raw RTSP OPTIONS/stage failure -1 → audio/platform unwind →
production cancel log/raw -1 → local setup -7 → CLOSED → native error termination → paired
reconnect without PIN → four Apps/rebuild → same-app encrypted RTSP/setup=0 → PASS late_start=0.
There is no first STARTED/input startup/decoder/Stream page before CLOSED. This validates the
stage-entry cooperative interrupt and partial unwind, without testing a blocked RTSP read or
FAILED cleanup after video buffers were allocated.

Recovery receives video at 400 ms and decodes 208 frames. Final double PS stops the owner with
result=0, audio port drain/release succeed and all three deferred video unmaps return 0.
Recovery initializes audio but explicitly reports no received audio before stop. The maintainer
subsequently confirms audible sound on Vita for this run. Preserve that user report alongside
the contradictory warning; packet receipt is not log-verified and the discrepancy is unresolved.
No Launch marker/current Sunshine version/error-OK timing is supplied.
USER_RW free is 33880 KiB initially/after reconnect and 33600 after final buffer release; this
single run is not a leak diagnosis/proof. No runtime-code change or agent build/device run.
Next cover cancellation after video allocation; earlier-entry replay, late cancelled-success,
blocked I/O, shutdown and repeats remain separate.

### After-video-allocation checkpoint

Source prepared in 2c11d3b; the targeted maintainer Vita result is recorded below. The procedure
remains available for reproduction; one passing run does not validate the full cancellation matrix.

The same startup mode accepts `MOONLIGHT_STARTUP_DISCONNECT_STAGE=10` (stage 4 remains the
default). Local and current upstream common-c Connection.c place the audio-start callback
after successful startVideoStream/stageComplete(9), before startAudioStream and connectionStarted.
[VideoStream.c](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/VideoStream.c)
calls the renderer setup before starting video threads. This boundary exercises actual Vita
decoder/framebuffer ownership before the first frontend STARTED. It does not simulate an
allocation failure or cancel inside a blocked renderer call.

Maintainer procedure (no agent build/device run):

1. Build/install the test package:

   ```bash
   ./build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_STARTUP_DISCONNECT_TEST=ON -DMOONLIGHT_STARTUP_DISCONNECT_STAGE=10
   ```

2. Enable Debug log, connect and start one app. The controller checks three owned framebuffer
   slots and a nonzero pool generation, then issues the real Disconnect once from the main thread:

   ```text
   [DisconnectStartupTest] checkpoint after video setup stage=10 buffers=3 generation=...
   [DisconnectStartupTest] startup disconnect request result=0
   [Stream] common-c startup cancelled result=...; owner cleanup returned
   [Stream] setup finished app=... result=-7
   [DisconnectStartupTest] cancelled setup observed result=-7 app=...
   [DisconnectStartupTest] CLOSED after cancelled startup; dismiss launch error with OK
   ```

   No first frontend STREAM_STARTED/STREAM_STOPPED or Stream page is accepted. Common-c's raw
   return can be an error or success followed by owner stop; do not require a stage-10 error.
   Audio/input transport stages can run before cooperative cancellation unwinds. Local result -7
   must follow owner teardown in either path.
3. Dismiss the ordinary cancellation error with OK. Record any human pause. Existing Apps
   STREAM_FAILED handling must schedule deferred main-thread buffer release. Before reconnect,
   require all three old-pool `frame[i] release unmap result=0x00000000` logs and
   `deferred frame buffers released`, followed by:

   ```text
   [DisconnectStartupTest] cancelled video buffers released generation=...
   [DisconnectStartupTest] reconnect enqueue result=0
   ```

   Unmaps may finish before OK/CLOSED; their required order is after owner cleanup and before
   reconnect/new pool allocation. The controller reads framebuffer metadata only on the owner
   at the checkpoint or on the main thread after CLOSED. It waits without blocking PAF until
   all slots of the captured generation clear; a changed generation or 60-second post-OK step
   timeout fails without another automated request. Zero metadata alone does not establish
   native unmap/free success; inspect the real unmap results. Kernel free success is not
   separately logged by the existing release code.
4. Require paired reconnect without PIN, fresh Apps and same-app recovery:

   ```text
   [DisconnectStartupTest] PASS startup cancelled -> CLOSED -> reconnect -> stream started app=... late_start=0 stage=10
   ```

   Confirm visible image and audible sound, then stop by double PS and capture the final audio/
   framebuffer cleanup tail. Supply the complete log from `[Launch]`, exact Sunshine version
   and error-OK timing. A host pending-session/RTSP recovery failure remains separate evidence;
   no automatic delay/retry, key reuse or remote quit is introduced.
5. Restore the normal package with `./build.sh -DMOONLIGHT_SYSTEM_APP=ON`.

Both observer and stage gate are test-only; no production cleanup/allocation path or vendored
dependency changes. build.sh resets the selector to 4 before explicit arguments, preventing a
cached stage 10 from silently affecting the next stage-4 build. CMake permits only 4 or 10 when
this mode is enabled. All five test modes remain default OFF and mutually exclusive.
The 88 Python source checks pass, including stage selection, partial/missing pool rejection,
release-before-reconnect, changed-generation/timeout rejection and one-shot recovery. The observer
is interpreted over all 512 combinations of three slots' memblock/pointer/mapping presence.
These serial mocks do not validate C++ compilation, real atomics, GPU/decoder lifetime or Vita behavior.

### Maintainer after-video-allocation result, 2026-10-21

Host .134/app 1093255277 reaches the stage-10 checkpoint with three mapped buffers/generation=1.
Disconnect=0 wins before frontend STARTED. Common-c still completes audio/input startup and
calls the connection callback; the local callback suppresses input/STARTED and the owner stops
the successful transport. Audio drain/release return 0, video decoded frames=0 and full common-c
cleanup precede raw result=0/local setup=-7, expected native error and CLOSED.

All three old unmaps return 0 and deferred release completes before native error termination.
The captured-generation-clear marker precedes reconnect, PAIRED without PIN, fresh four Apps/
correct rebuild, same-app resume/encrypted RTSP/setup=0 and PASS late_start=0 stage=10.
No first frontend STARTED/STREAM_STOPPED/Stream page is present; the cancelled attempt's no-audio/
video warnings are expected for its immediate teardown, not recovery warnings.

Recovery receives video at 500 ms and audio at 3000 ms, then decodes 445 frames. An OOS/RFI/IDR
wait/drop-limit/request sequence occurs before decoding resumes; this is not an error-free media
quality result. Final double PS produces owner stop=0, audio drain/release=0 and three successful
unmaps/deferred release. The maintainer confirms image/audible sound and Sunshine 2025.924.154138.
No Launch marker/exact installed revision or error-OK timing is supplied; identify the test behavior,
not an exact binary hash. USER_RW free: 33872 KiB initially, 33868 at paired reconnect and 33592
after final release. A single cycle is neither leak diagnosis nor leak-free proof.

This verifies the targeted cancelled-success owner cleanup/FAILED deferred-buffer path and
recovery on one Vita. Earlier-entry races, blocked-I/O latency, shutdown, repeats and host-side
cleanup remain separate. No runtime-code change or agent build/device run for this record.
Next restore ordinary test-OFF packaging and check repeated start/stop cycles.

## Ordinary repeated start/stop — maintainer result, 2026-10-21

The requested ordinary run identifies system build 98f0d44194af, host .134/app 1093255277;
no test-controller markers or automatic disconnects appear. One paired Connect/Apps precedes
three same-app resumes/encrypted RTSP/setup=0. Decoded totals are 1833, 1856 and 1840 frames;
video packet delays are 400/400/300 ms and audio delays 1700/0/0 ms. Each double PS completes
owner stop=0, audio drain/release=0, infobar restore and three successful unmaps/deferred release.
All old releases precede the next Start. OOS/RFI/audio-recovery messages occur with continued
decoding; no setup/allocation failure or unexpected termination appears. This proves the
sequential protocol/resource cycle, not error-free media quality or physical playback by itself.

Equivalent post-release USER_RW free is 33596 KiB in all three cycles (initial paired idle
33876 KiB); stream-ready free is 10828 KiB each time. The newlib arena stays 196608 bytes while
used is 98792 → 99096 → 99256 (+464 bytes over two repeats). Preserve that small rise alongside
the stable kernel samples; neither a leak diagnosis nor leak-free proof follows from this run.
Visible image/audible sound and current Sunshine release require separate confirmation.

There is no host DisconnectHost/CLOSED/reconnect or application shutdown between starts.
The trace predates ee485a2's Main-label fix. Repeated failure recovery, active shutdown,
callback/admission races and host-side cleanup remain separate. No runtime-code change or
agent build/device run. Full evidence/limits: API audit F5; next inspect/prepare active-stream
application-shutdown validation.
