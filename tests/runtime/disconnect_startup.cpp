#include <string.h>
#include <Limelight.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "app/disconnect_test.h"
#include "app/moonlight_app.h"
#include "debug.h"

namespace {

enum TestPhase {
    AwaitApplications,
    AwaitFirstStart,
    AwaitCheckpoint,
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
static bool s_saw_started = false;
static bool s_saw_stopped = false;
static int s_application_id = -1;
static uint64_t s_step_started_us = 0;
static const uint64_t kStepTimeoutUs = 60000000;
/* Shared with the stream worker. All remaining scenario state is main-thread-only. */
static int s_checkpoint_used = 0;
static int s_checkpoint_status = 0; /* 0 absent, 1 waiting, 2 released, -1 timed out */
static int s_checkpoint_release = 0;

static void release_checkpoint()
{
    __atomic_store_n(&s_checkpoint_release, 1, __ATOMIC_RELEASE);
}

static void set_phase(TestPhase phase)
{
    s_phase = phase;
    s_step_started_us = sceKernelGetProcessTimeWide();
}

static void fail(const char *reason, int result)
{
    release_checkpoint();
    vita_debug_log("[DisconnectStartupTest] FAIL phase=%d reason=%s result=%d",
                   (int)s_phase, reason, result);
    s_phase = Finished;
}

}

/* Sole LiStartConnection owner: stop at RTSP stage entry after audio/platform init.
 * No PAF, stream/control mutex, protocol interruption or production cancel mutation. */
extern "C" void disconnect_test_startup_checkpoint(int stage)
{
    if (stage != STAGE_RTSP_HANDSHAKE) return;
    if (__atomic_exchange_n(&s_checkpoint_used, 1, __ATOMIC_ACQ_REL)) return;
    if (__atomic_load_n(&s_checkpoint_release, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&s_checkpoint_status, 1, __ATOMIC_RELEASE);
    vita_debug_log("[DisconnectStartupTest] checkpoint inside LiStartConnection stage=%d before RTSP handshake", stage);
    const uint64_t entered_us = sceKernelGetProcessTimeWide();
    while (!__atomic_load_n(&s_checkpoint_release, __ATOMIC_ACQUIRE)) {
        if (sceKernelGetProcessTimeWide() - entered_us > kStepTimeoutUs) {
            __atomic_store_n(&s_checkpoint_status, -1, __ATOMIC_RELEASE);
            vita_debug_log("[DisconnectStartupTest] FAIL checkpoint timeout; worker released");
            return;
        }
        sceKernelDelayThread(10000);
    }
    __atomic_store_n(&s_checkpoint_status, 2, __ATOMIC_RELEASE);
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
    vita_debug_log("[DisconnectStartupTest] armed host=%s port=%u", host.internal, host.port);
}

void disconnect_test_capture_start(int application_id, int result)
{
    if (s_phase == Finished) return;
    if (s_phase == AwaitFirstStart && result == 0 && application_id >= 0) {
        s_application_id = application_id;
        set_phase(AwaitCheckpoint);
        vita_debug_log("[DisconnectStartupTest] start accepted app=%d; awaiting checkpoint", application_id);
    } else if (s_phase != AwaitRestarted || application_id != s_application_id || result != 0) {
        fail("unexpected/rejected Start", result);
    }
}

void disconnect_test_poll(MoonlightApp &app)
{
    if (s_phase == AwaitApplications || s_phase == AwaitFirstStart || s_phase == Finished) return;
    if (__atomic_load_n(&s_checkpoint_status, __ATOMIC_ACQUIRE) < 0) {
        fail("checkpoint timed out", -1);
        return;
    }
    if (sceKernelGetProcessTimeWide() - s_step_started_us > kStepTimeoutUs) {
        fail("step timeout; no further test actions", -1);
        return;
    }

    int result;
    if (s_phase == AwaitCheckpoint) {
        if (__atomic_load_n(&s_checkpoint_status, __ATOMIC_ACQUIRE) != 1) return;
        if (app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED || !app.Connection().IsBusy()) {
            fail("invalid startup checkpoint/state", -1);
            return;
        }
        s_saw_started = false;
        s_saw_stopped = false;
        set_phase(AwaitClosed);
        /* Deliberately call Disconnect while the launch foreground operation is still busy. */
        result = app.Connection().Disconnect();
        vita_debug_log("[DisconnectStartupTest] startup disconnect request result=%d", result);
        if (result != 0) fail("disconnect rejected", result);
        /* A successful request has set the real stream cancel flag before releasing the worker. */
        release_checkpoint();
        return;
    }
    if (app.Connection().IsBusy()) return;

    switch (s_phase) {
    case NeedReconnect:
        if (app.Connection().State() != MOONLIGHT_CONNECTION_DISCONNECTED) {
            fail("state changed before reconnect", (int)app.Connection().State());
            return;
        }
        set_phase(AwaitReconnect);
        result = app.Connection().Connect(s_host);
        vita_debug_log("[DisconnectStartupTest] reconnect enqueue result=%d", result);
        if (result != 0) fail("reconnect rejected", result);
        break;

    case NeedRefresh:
        set_phase(AwaitRefresh);
        result = app.Applications().Refresh();
        vita_debug_log("[DisconnectStartupTest] refresh enqueue result=%d", result);
        if (result != 0) fail("refresh rejected", result);
        break;

    case NeedRestart:
        if (app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("state changed before restart", (int)app.Connection().State());
            return;
        }
        set_phase(AwaitRestarted);
        result = app.Connection().Start(s_application_id);
        vita_debug_log("[DisconnectStartupTest] restart request app=%d result=%d", s_application_id, result);
        if (result != 0) fail("restart rejected", result);
        break;

    default:
        break;
    }
}

void disconnect_test_event(MoonlightApp &app, const MoonlightEvent &event)
{
    if (!s_have_host || s_phase == Finished) return;
    if (event.type == MOONLIGHT_EVENT_STREAM_STARTED && s_phase == AwaitClosed) {
        if (s_saw_started || s_saw_stopped || event.result != 0 ||
            event.application_id != s_application_id || !event.address ||
            strcmp(event.address, s_host.internal) != 0) {
            fail("invalid/duplicate late stream start", event.result);
            return;
        }
        /* Adapter state can already be DISCONNECTED while PAF drains queued events. */
        s_saw_started = true;
        vita_debug_log("[DisconnectStartupTest] late STREAM_STARTED after Disconnect; awaiting owner STOPPED app=%d", s_application_id);
        return;
    }
    if (event.type == MOONLIGHT_EVENT_STREAM_STOPPED) {
        if (s_phase != AwaitClosed || !s_saw_started || s_saw_stopped || event.result != 0 ||
            event.application_id != s_application_id || !event.address ||
            strcmp(event.address, s_host.internal) != 0) {
            fail("STOPPED before late start or invalid result/host/app", event.result);
            return;
        }
        s_saw_stopped = true;
        vita_debug_log("[DisconnectStartupTest] owner STOPPED observed app=%d", s_application_id);
        return;
    }
    /* Real startup errors are failures, never normalized to -7 or successful cancellation. */
    if (event.type == MOONLIGHT_EVENT_STREAM_FAILED) {
        fail("startup/recovery failed; preserve actual result and cleanup tail", event.result);
        return;
    }
    if (event.type == MOONLIGHT_EVENT_CONNECTION_FAILED ||
        event.type == MOONLIGHT_EVENT_APPLICATIONS_FAILED ||
        event.type == MOONLIGHT_EVENT_PAIRING_FAILED) {
        fail("unexpected backend failure/stop", event.result);
        return;
    }
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
            set_phase(AwaitFirstStart);
            vita_debug_log("[DisconnectStartupTest] ready; start one app; late start/owner stop may briefly open Stream");
        } else {
            set_phase(NeedRestart);
            vita_debug_log("[DisconnectStartupTest] refreshed applications count=%d", event.result);
        }
    } else if (event.type == MOONLIGHT_EVENT_CONNECTION_CLOSED) {
        if (s_phase != AwaitClosed || !s_saw_started || !s_saw_stopped || event.result != 0 || !event.address ||
            strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_DISCONNECTED) {
            fail("CLOSED before owner STOPPED or invalid host/result/state", event.result);
            return;
        }
        set_phase(NeedReconnect);
        vita_debug_log("[DisconnectStartupTest] CLOSED after owner STOPPED; state=DISCONNECTED");
    } else if (event.type == MOONLIGHT_EVENT_CONNECTION_READY && s_phase == AwaitReconnect) {
        if (event.result != 0 || !event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_PAIRED) {
            fail("reconnect host/trust/state mismatch", event.result);
            return;
        }
        set_phase(NeedRefresh);
        vita_debug_log("[DisconnectStartupTest] reconnect ready state=PAIRED");
    } else if (event.type == MOONLIGHT_EVENT_STREAM_STARTED) {
        if (s_phase != AwaitRestarted || event.result != 0 || event.application_id != s_application_id ||
            !event.address || strcmp(event.address, s_host.internal) != 0 ||
            app.Connection().State() != MOONLIGHT_CONNECTION_STREAMING) {
            fail("STREAM_STARTED before recovery or wrong host/app/state", event.result);
            return;
        }
        s_phase = Finished;
        vita_debug_log("[DisconnectStartupTest] PASS startup owner STOPPED -> CLOSED -> reconnect -> stream started app=%d late_start=1",
                       s_application_id);
    }
}

void disconnect_test_shutdown()
{
    release_checkpoint();
    if (s_phase != AwaitApplications && s_phase != Finished) {
        fail("application shutdown interrupted test", -1);
    }
    s_phase = Finished;
}
