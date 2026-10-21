# Vita idle DisconnectHost smoke test

This opt-in package performs one local **idle** disconnect and reconnect to the PC selected
through the existing UI. It uses ConnectionService/ApplicationService and MoonlightApp's
main-thread event delivery. It adds no button, endpoint, host credentials or persistent setting.
It does not launch/quit a game or revoke pairing. Pending-launch/active-stream disconnect,
duplicate/admission races, failure rollback and shutdown remain separate tests.

## Maintainer steps

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

   `build.sh` explicitly defaults this test OFF on each invocation. Direct CMake users must
   pass `-DMOONLIGHT_DISCONNECT_TEST=OFF` to clear a previously enabled cache entry.

## Scope and implementation

The option defaults OFF. Its source and main-thread hooks are compiled only with the option ON.
The original selected MoonlightHost is captured by ConnectionService after foreground reservation;
the hook includes custom port/identity data instead of guessing the PC from list order. The test
observes events after PrepareCompletion and runs commands on the next PAF poll. Connect and app
refresh still use their existing jobs/native busy dialogs; Disconnect uses its existing cleanup
worker. The scenario replaces no frontend callback and performs no UI work on a worker thread.
Changing PCs during the active test aborts it. Ordinary builds contain no test logs or actions.

PASS establishes the observed idle CLOSED → paired reconnect → authenticated application-list
sequence only. It does not establish absence of leaks, exact animation timing, streamed teardown,
hardware input behavior, API thread safety or the full F5 matrix. No Vita build/device result has
yet been supplied for this test mode. Python source-interpretation checks mock service calls and
time; they do not compile C++ or execute PAF/network operations.
