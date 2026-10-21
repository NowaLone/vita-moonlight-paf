#include <string.h>
#include <psp2/kernel/processmgr.h>

#include "app/disconnect_test.h"
#include "app/moonlight_app.h"
#include "debug.h"

namespace {

enum TestPhase {
    AwaitApplications,
    NeedDisconnect,
    AwaitClosed,
    NeedReconnect,
    AwaitReconnect,
    NeedRefresh,
    AwaitRefresh,
    Finished
};

static TestPhase s_phase = AwaitApplications;
static MoonlightHost s_host;
static bool s_have_host = false;
static uint64_t s_step_started_us = 0;
static const uint64_t kStepTimeoutUs = 60000000;

static void set_phase(TestPhase phase)
{
    s_phase = phase;
    s_step_started_us = sceKernelGetProcessTimeWide();
}

static void fail(const char *reason, int result)
{
    vita_debug_log("[DisconnectTest] FAIL phase=%d reason=%s result=%d",
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
    vita_debug_log("[DisconnectTest] armed host=%s port=%u", host.internal, host.port);
}

void disconnect_test_poll(MoonlightApp &app)
{
    if (s_phase == AwaitApplications || s_phase == Finished) return;
    if (sceKernelGetProcessTimeWide() - s_step_started_us > kStepTimeoutUs) {
        fail("step timeout; no further test actions", -1);
        return;
    }
    if (app.Connection().IsBusy()) return;

    int result;
    switch (s_phase) {
    case NeedDisconnect:
        if (app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("expected paired idle host", (int)app.Connection().State());
            return;
        }
        set_phase(AwaitClosed);
        result = app.Connection().Disconnect();
        vita_debug_log("[DisconnectTest] disconnect request result=%d", result);
        if (result != 0) fail("disconnect rejected", result);
        break;

    case NeedReconnect:
        if (app.Connection().State() != MOONLIGHT_CONNECTION_DISCONNECTED) {
            fail("state changed before reconnect", (int)app.Connection().State());
            return;
        }
        set_phase(AwaitReconnect);
        result = app.Connection().Connect(s_host);
        vita_debug_log("[DisconnectTest] reconnect enqueue result=%d", result);
        if (result != 0) fail("reconnect rejected", result);
        break;

    case NeedRefresh:
        set_phase(AwaitRefresh);
        result = app.Applications().Refresh();
        vita_debug_log("[DisconnectTest] refresh enqueue result=%d", result);
        if (result != 0) fail("refresh rejected", result);
        break;

    default:
        break;
    }
}

void disconnect_test_event(MoonlightApp &app, const MoonlightEvent &event)
{
    if (!s_have_host || s_phase == Finished) return;

    if (s_phase == AwaitApplications) {
        if (event.type == MOONLIGHT_EVENT_APPLICATIONS_READY && event.result >= 0 &&
            app.Connection().State() == MOONLIGHT_CONNECTION_PAIRED) {
            if (!event.address || strcmp(event.address, s_host.internal) != 0) {
                fail("initial application-list host mismatch", -1);
                return;
            }
            vita_debug_log("[DisconnectTest] begin idle disconnect/reconnect; leave controls untouched");
            set_phase(NeedDisconnect);
        }
        return;
    }

    if (event.type == MOONLIGHT_EVENT_CONNECTION_FAILED ||
        event.type == MOONLIGHT_EVENT_APPLICATIONS_FAILED) {
        fail("backend operation failed", event.result);
        return;
    }
    if (event.type == MOONLIGHT_EVENT_PAIRING_REQUIRED ||
        event.type == MOONLIGHT_EVENT_STREAM_STARTED) {
        fail("unexpected pairing/stream event", (int)event.type);
        return;
    }

    if (s_phase == AwaitClosed && event.type == MOONLIGHT_EVENT_CONNECTION_CLOSED) {
        if (event.result != 0 || !event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_DISCONNECTED) {
            fail("invalid CLOSED result/host/state", event.result);
            return;
        }
        vita_debug_log("[DisconnectTest] CLOSED observed state=DISCONNECTED");
        set_phase(NeedReconnect);
    } else if (s_phase == AwaitReconnect && event.type == MOONLIGHT_EVENT_CONNECTION_READY) {
        if (!event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("reconnect host/trust/state mismatch", event.result);
            return;
        }
        vita_debug_log("[DisconnectTest] reconnect ready state=PAIRED");
        set_phase(NeedRefresh);
    } else if ((s_phase == AwaitRefresh || s_phase == NeedRefresh) &&
               event.type == MOONLIGHT_EVENT_APPLICATIONS_READY) {
        if (event.result < 0 || app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("invalid refreshed list/state", event.result);
            return;
        }
        vita_debug_log("[DisconnectTest] PASS idle CLOSED -> reconnect -> applications count=%d",
                       event.result);
        s_phase = Finished;
    }
}

void disconnect_test_shutdown()
{
    if (s_phase != AwaitApplications && s_phase != Finished) {
        fail("application shutdown interrupted test", -1);
    }
    s_phase = Finished;
}
