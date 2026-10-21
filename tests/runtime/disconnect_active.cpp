#include <string.h>
#include <psp2/kernel/processmgr.h>

#include "app/disconnect_test.h"
#include "app/moonlight_app.h"
#include "debug.h"

namespace {

enum TestPhase {
    AwaitApplications,
    AwaitFirstStream,
    HoldStream,
    AwaitClosed,
    NeedReconnect,
    AwaitReconnect,
    NeedRefresh,
    AwaitRefresh,
    NeedRestart,
    AwaitRestarted,
    Finished
};

static TestPhase s_phase = AwaitApplications;
static MoonlightHost s_host;
static bool s_have_host = false;
static bool s_saw_stopped = false;
static int s_application_id = -1;
static uint64_t s_step_started_us = 0;
static const uint64_t kStepTimeoutUs = 60000000;
static const uint64_t kStreamHoldUs = 3000000;

static void set_phase(TestPhase phase)
{
    s_phase = phase;
    s_step_started_us = sceKernelGetProcessTimeWide();
}

static void fail(const char *reason, int result)
{
    vita_debug_log("[DisconnectActiveTest] FAIL phase=%d reason=%s result=%d",
                   (int)s_phase, reason, result);
    s_phase = Finished;
}

}

void disconnect_test_capture_host(const MoonlightHost &host)
{
    if (s_phase == Finished) return;
    if (s_phase != AwaitApplications) {
        if (s_phase == AwaitReconnect && host.port == s_host.port &&
            strcmp(host.internal, s_host.internal) == 0) return;
        fail("another Connect interrupted test", -1);
        return;
    }
    s_host = host;
    s_have_host = true;
    vita_debug_log("[DisconnectActiveTest] armed host=%s port=%u", host.internal, host.port);
}

void disconnect_test_poll(MoonlightApp &app)
{
    /* The user chooses when/which app to start. No automatic launch from the list. */
    if (s_phase == AwaitApplications || s_phase == AwaitFirstStream || s_phase == Finished) return;
    if (sceKernelGetProcessTimeWide() - s_step_started_us > kStepTimeoutUs) {
        fail("step timeout; no further test actions", -1);
        return;
    }
    if (app.Connection().IsBusy()) return;

    int result;
    switch (s_phase) {
    case HoldStream:
        if (sceKernelGetProcessTimeWide() - s_step_started_us < kStreamHoldUs) return;
        if (app.Connection().State() != MOONLIGHT_CONNECTION_STREAMING) {
            fail("expected active stream", (int)app.Connection().State());
            return;
        }
        s_saw_stopped = false;
        set_phase(AwaitClosed);
        result = app.Connection().Disconnect();
        vita_debug_log("[DisconnectActiveTest] disconnect request result=%d", result);
        if (result != 0) fail("disconnect rejected", result);
        break;

    case NeedReconnect:
        if (app.Connection().State() != MOONLIGHT_CONNECTION_DISCONNECTED) {
            fail("state changed before reconnect", (int)app.Connection().State());
            return;
        }
        set_phase(AwaitReconnect);
        result = app.Connection().Connect(s_host);
        vita_debug_log("[DisconnectActiveTest] reconnect enqueue result=%d", result);
        if (result != 0) fail("reconnect rejected", result);
        break;

    case NeedRefresh:
        set_phase(AwaitRefresh);
        result = app.Applications().Refresh();
        vita_debug_log("[DisconnectActiveTest] refresh enqueue result=%d", result);
        if (result != 0) fail("refresh rejected", result);
        break;

    case NeedRestart:
        if (app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("state changed before restart", (int)app.Connection().State());
            return;
        }
        set_phase(AwaitRestarted);
        result = app.Connection().Start(s_application_id);
        vita_debug_log("[DisconnectActiveTest] restart request app=%d result=%d", s_application_id, result);
        if (result != 0) fail("restart rejected", result);
        break;

    default:
        break;
    }
}

void disconnect_test_event(MoonlightApp &app, const MoonlightEvent &event)
{
    if (!s_have_host || s_phase == Finished) return;
    if (event.type == MOONLIGHT_EVENT_CONNECTION_FAILED ||
        event.type == MOONLIGHT_EVENT_APPLICATIONS_FAILED ||
        event.type == MOONLIGHT_EVENT_STREAM_FAILED ||
        event.type == MOONLIGHT_EVENT_PAIRING_FAILED) {
        fail("backend operation failed", event.result);
        return;
    }
    /* Initial PIN pairing is allowed; saved trust must suffice after disconnect. */
    if (event.type == MOONLIGHT_EVENT_PAIRING_REQUIRED && s_phase != AwaitApplications) {
        fail("unexpected pairing request", event.result);
        return;
    }

    if (event.type == MOONLIGHT_EVENT_APPLICATIONS_READY) {
        if (s_phase != AwaitApplications && s_phase != AwaitRefresh && s_phase != NeedRefresh) return;
        if (event.result < 0 || !event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("invalid application-list host/result/state", event.result);
            return;
        }
        if (s_phase == AwaitApplications) {
            set_phase(AwaitFirstStream);
            vita_debug_log("[DisconnectActiveTest] ready; start one app, then leave controls untouched");
        } else {
            set_phase(NeedRestart);
            vita_debug_log("[DisconnectActiveTest] refreshed applications count=%d", event.result);
        }
    } else if (event.type == MOONLIGHT_EVENT_STREAM_STARTED) {
        if ((s_phase != AwaitFirstStream && s_phase != AwaitRestarted) || event.result != 0 ||
            event.application_id < 0 || !event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_STREAMING) {
            fail("unexpected stream start/host/state", event.result);
            return;
        }
        if (s_phase == AwaitFirstStream) {
            s_application_id = event.application_id;
            set_phase(HoldStream);
            vita_debug_log("[DisconnectActiveTest] active app=%d; disconnect in 3 seconds", s_application_id);
        } else {
            if (event.application_id != s_application_id) {
                fail("restart application mismatch", event.application_id);
                return;
            }
            s_phase = Finished;
            vita_debug_log("[DisconnectActiveTest] PASS active STOPPED -> CLOSED -> reconnect -> stream started app=%d",
                           s_application_id);
        }
    } else if (event.type == MOONLIGHT_EVENT_STREAM_STOPPED) {
        if (s_phase != AwaitClosed || s_saw_stopped || event.result != 0 ||
            event.application_id != s_application_id || !event.address ||
            strcmp(event.address, s_host.internal) != 0) {
            fail("unexpected stream stop/result/host/app", event.result);
            return;
        }
        /* Backend state may already be DISCONNECTED while queued STOPPED is delivered. */
        s_saw_stopped = true;
        vita_debug_log("[DisconnectActiveTest] STOPPED observed app=%d", s_application_id);
    } else if (event.type == MOONLIGHT_EVENT_CONNECTION_CLOSED) {
        if (s_phase != AwaitClosed || !s_saw_stopped || event.result != 0 || !event.address ||
            strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_DISCONNECTED) {
            fail("CLOSED before STOPPED or invalid host/result/state", event.result);
            return;
        }
        set_phase(NeedReconnect);
        vita_debug_log("[DisconnectActiveTest] CLOSED observed state=DISCONNECTED after STOPPED");
    } else if (event.type == MOONLIGHT_EVENT_CONNECTION_READY && s_phase == AwaitReconnect) {
        if (event.result != 0 || !event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("reconnect host/trust/state mismatch", event.result);
            return;
        }
        set_phase(NeedRefresh);
        vita_debug_log("[DisconnectActiveTest] reconnect ready state=PAIRED");
    }
}

void disconnect_test_shutdown()
{
    if (s_phase != AwaitApplications && s_phase != Finished) {
        fail("application shutdown interrupted test", -1);
    }
    s_phase = Finished;
}
