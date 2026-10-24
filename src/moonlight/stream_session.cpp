#include "moonlight/stream_session.h"
#include "moonlight/gamestream_xml.h"
#if defined(MOONLIGHT_PENDING_DISCONNECT_TEST) || defined(MOONLIGHT_STARTUP_DISCONNECT_TEST)
#include "app/disconnect_test.h"
#endif
#ifdef MOONLIGHT_HTTP_DISCONNECT_TEST
#include "moonlight/http_disconnect_test.h"
#endif

#include <arpa/inet.h>
#include <psp2/kernel/rng.h>
#include <pthread.h>
#include <psp2/kernel/threadmgr.h>

#include <Limelight.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "app/memory_budget.h"
#include "app/heap_trace.h"
#include "vita_video_renderer.h"
#include "vita_audio_renderer.h"
#include "vita_input.h"
#include "stream_stats_internal.h"

namespace {

struct StreamContext {
    MoonlightStreamEventCallback callback;
    void *userdata;
    int started;
};

struct StreamThreadArgs {
    LegacyGameStreamServer *server;
    int application_id;
    int quit_application_id;
    MoonlightSettings settings;
    uint32_t stats_generation;
};

static StreamContext s_stream_context = { NULL, NULL, 0 };
/* One owner reserves its thread before setup and keeps it until transport teardown. */
static pthread_mutex_t s_stream_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_stream_thread_running = 0;
static int s_stream_stop_requested = 0;
static int s_stream_cancel_requested = 0;
static int s_stream_termination_result = 0;
static uint32_t s_stream_stats_generation;
/* Published by the first stage callback, after common-c resets its interrupt flag.
 * These fields and LiInterruptConnection admission are protected by s_stream_mutex. */
static int s_common_starting = 0;
static int s_start_interrupt_sent = 0;
/* Same mutex as stream admission/stop: a stop wins over later RGB feedback.
 * The owner joins common-c's callbacks before admitting another session. */
static MoonlightControllerLight s_controller_light = {};

struct RecoveryWaitLog {
    uint64_t last_emit_us;
    unsigned int seen;
    unsigned int emitted;
    unsigned int pending;
};

static pthread_mutex_t s_recovery_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static RecoveryWaitLog s_recovery_wait_logs[2] = {};

static bool update_recovery_wait_log(
    RecoveryWaitLog *state, uint64_t now_us, unsigned int *omitted)
{
    ++state->seen;
    if (state->emitted && now_us - state->last_emit_us < 1000000ull) {
        ++state->pending;
        return false;
    }
    *omitted = state->pending;
    state->pending = 0;
    state->last_emit_us = now_us;
    ++state->emitted;
    return true;
}

static void reset_recovery_wait_logs(void)
{
    pthread_mutex_lock(&s_recovery_log_mutex);
    memset(s_recovery_wait_logs, 0, sizeof(s_recovery_wait_logs));
    pthread_mutex_unlock(&s_recovery_log_mutex);
}

/* Called only after common-c has joined its transport threads. */
static void summarize_recovery_wait_logs(void)
{
    RecoveryWaitLog snapshot[2];
    pthread_mutex_lock(&s_recovery_log_mutex);
    memcpy(snapshot, s_recovery_wait_logs, sizeof(snapshot));
    pthread_mutex_unlock(&s_recovery_log_mutex);

    for (int kind = 0; kind < 2; ++kind) {
        if (snapshot[kind].seen > snapshot[kind].emitted) {
            vita_debug_log(
                "[Limelight] %s wait log summary: messages=%u omitted=%u",
                kind == 0 ? "IDR" : "RFI", snapshot[kind].seen,
                snapshot[kind].seen - snapshot[kind].emitted);
        }
    }
}

static int stream_cancel_requested(void)
{
    pthread_mutex_lock(&s_stream_mutex);
    int cancelled = s_stream_cancel_requested;
    pthread_mutex_unlock(&s_stream_mutex);
    return cancelled;
}

static void emit_event(MoonlightEventType type, int result)
{
    pthread_mutex_lock(&s_stream_mutex);
    MoonlightStreamEventCallback callback = s_stream_context.callback;
    void *userdata = s_stream_context.userdata;
    pthread_mutex_unlock(&s_stream_mutex);
    if (callback) {
        callback(type, result, userdata);
    }
}

/* Publish completion only after cleanup, and never read a subsequent session's callback. */
static void finish_stream_worker(MoonlightEventType type, int result)
{
    pthread_mutex_lock(&s_stream_mutex);
    MoonlightStreamEventCallback callback = s_stream_context.callback;
    void *userdata = s_stream_context.userdata;
    s_stream_context.started = 0;
    pthread_mutex_unlock(&s_stream_mutex);
    if (callback) {
        callback(type, result, userdata);
    }
    /* Disconnect cleanup must not overtake the API's stop-event bookkeeping. */
    pthread_mutex_lock(&s_stream_mutex);
    s_stream_stop_requested = 0;
    s_stream_cancel_requested = 0;
    s_stream_thread_running = 0;
    pthread_mutex_unlock(&s_stream_mutex);
}

/* Nonblocking notification only; never run LiStopConnection from a requesting thread. */
static void interrupt_common_startup_locked(void)
{
    if (s_common_starting && s_stream_cancel_requested &&
        !s_stream_context.started && !s_start_interrupt_sent) {
        LiInterruptConnection();
        s_start_interrupt_sent = 1;
    }
}

static void publish_common_startup(void)
{
    pthread_mutex_lock(&s_stream_mutex);
    s_common_starting = 1;
    /* Replay a stop that arrived between the pre-call check and common-c's reset. */
    interrupt_common_startup_locked();
    pthread_mutex_unlock(&s_stream_mutex);
}

/* Sole LiStartConnection owner, after it returned and unwound any failed setup. */
static int finish_common_startup(int result)
{
    pthread_mutex_lock(&s_stream_mutex);
    s_common_starting = 0;
    int cancelled = s_start_interrupt_sent && s_stream_cancel_requested &&
                    !s_stream_context.started;
    s_start_interrupt_sent = 0;
    pthread_mutex_unlock(&s_stream_mutex);
    if (cancelled) {
        /* A late interrupt may race the successful return. No STARTED was published;
         * the owner must stop that successful transport before reporting failure. */
        if (result == 0) LiStopConnection();
        vita_debug_log("[Stream] common-c startup cancelled result=%d; owner cleanup returned", result);
        return -7;
    }
    return result;
}

static void on_stage_start(int stage)
{
    publish_common_startup();
    vita_debug_log(
        "[Stream] stage start %d (%s)",
        stage,
        LiGetStageName(stage));
#ifdef MOONLIGHT_STARTUP_DISCONNECT_TEST
    disconnect_test_startup_checkpoint(stage);
#endif
}

static void on_stage_complete(int stage)
{
    vita_debug_log(
        "[Stream] stage complete %d (%s)",
        stage,
        LiGetStageName(stage));
}

static void on_stage_failed(int stage, int result)
{
    vita_debug_log(
        "[Stream] stage failed %d (%s) err=%d",
        stage,
        LiGetStageName(stage),
        result);
}

static void on_connection_started(void)
{
    pthread_mutex_lock(&s_stream_mutex);
    s_common_starting = 0;
    const int cancelled = s_stream_cancel_requested;
    if (!cancelled) s_stream_context.started = 1;
    pthread_mutex_unlock(&s_stream_mutex);
    if (cancelled) {
        vita_debug_log("[Stream] connection ready after cancel; owner cleanup pending");
        return;
    }
    vita_input_start();
    vita_debug_log("[Stream] connection started");
    emit_event(MOONLIGHT_EVENT_STREAM_STARTED, 0);
    /* A stop after the started-state commit follows the ordinary active-stream owner path. */
}

static void on_connection_terminated(int result)
{
    /* common-c may call from its termination thread; the owner performs all cleanup. */
    pthread_mutex_lock(&s_stream_mutex);
    if (s_stream_thread_running) {
        s_stream_termination_result = result;
        s_stream_stop_requested = 1;
    }
    pthread_mutex_unlock(&s_stream_mutex);
    vita_debug_log("[Stream] connection terminated result=%d; cleanup requested", result);
}

static void on_status_update(int status)
{
    pthread_mutex_lock(&s_stream_mutex);
    if (s_stream_thread_running && !s_stream_stop_requested && !s_stream_cancel_requested) {
        moonlight_stats_connection_status(s_stream_stats_generation, status == CONN_STATUS_POOR);
    }
    pthread_mutex_unlock(&s_stream_mutex);
    vita_debug_log("[Stream] connection status=%d", status);
}

static void on_log_message(const char *format, ...)
{
    char buffer[512];
    va_list args;

    if (strcmp(format, "Video Receive: malloc() failed\n") == 0) {
        moonlight_log_heap_trace("video-receive-oom");
    }
    /* These exact common-c messages occur once per discarded waiting frame. */
    int kind = strcmp(format, "Waiting for IDR frame\n") == 0 ? 0 :
        (strcmp(format, "Waiting for RFI frame\n") == 0 ? 1 : -1);
    if (kind >= 0) {
        unsigned int omitted = 0;
        pthread_mutex_lock(&s_recovery_log_mutex);
        bool emit = update_recovery_wait_log(
            &s_recovery_wait_logs[kind], sceKernelGetSystemTimeWide(), &omitted);
        pthread_mutex_unlock(&s_recovery_log_mutex);
        if (emit) {
            vita_debug_log(
                "[Limelight] Waiting for %s frame (repeated logs omitted=%u)",
                kind == 0 ? "IDR" : "RFI", omitted);
        }
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    vita_debug_log("[Limelight] %s", buffer);
}

static void on_hdr_mode(bool enabled)
{
    vita_debug_log("[Stream] HDR mode=%d", enabled ? 1 : 0);
}

static void on_motion_event_state(
    uint16_t controller,
    uint8_t motion_type,
    uint16_t report_rate_hz)
{
    vita_input_set_motion_state(
        controller,
        motion_type,
        report_rate_hz);
}

static void on_controller_led(uint16_t controller, uint8_t r, uint8_t g, uint8_t b)
{
    pthread_mutex_lock(&s_stream_mutex);
    if (controller == 0 && s_stream_thread_running &&
        !s_stream_stop_requested && !s_stream_cancel_requested &&
        s_controller_light.mode != MOONLIGHT_CONTROLLER_LIGHT_OFF) {
        s_controller_light.r = r;
        s_controller_light.g = g;
        s_controller_light.b = b;
    }
    pthread_mutex_unlock(&s_stream_mutex);
}

static void initialize_connection_callbacks(
    CONNECTION_LISTENER_CALLBACKS *callbacks)
{
    LiInitializeConnectionCallbacks(callbacks);
    callbacks->stageStarting = on_stage_start;
    callbacks->stageComplete = on_stage_complete;
    callbacks->stageFailed = on_stage_failed;
    callbacks->connectionStarted = on_connection_started;
    callbacks->connectionTerminated = on_connection_terminated;
    callbacks->logMessage = on_log_message;
    callbacks->connectionStatusUpdate = on_status_update;
    callbacks->setHdrMode = on_hdr_mode;
    callbacks->setMotionEventState = on_motion_event_state;
    callbacks->setControllerLED = on_controller_led;
}

static int make_hex(
    const unsigned char *data,
    size_t length,
    char *output,
    size_t output_size)
{
    size_t i;

    if (!data || !output || output_size < length * 2 + 1) {
        return -1;
    }

    for (i = 0; i < length; ++i) {
        snprintf(output + i * 2, output_size - i * 2, "%02x", data[i]);
    }
    output[length * 2] = '\0';
    return 0;
}

static int load_launch_session(
    LegacyGameStreamServer *server,
    int application_id,
    int quit_application_id,
    const MoonlightSettings *settings,
    char *session_url,
    size_t session_url_size,
    unsigned char remote_key[16],
    unsigned char remote_iv[16])
{
    char rikey_hex[33];
    char query[4096];
    unsigned int rikey_id;
    char *body = NULL;
    size_t body_length = 0;
    MoonlightGameStreamResponse response;
    int result;

    if (!server || !server->curl || !settings ||
        !session_url || session_url_size == 0) {
        return -1;
    }

    if (stream_cancel_requested()) return -7;
    result = legacy_gamestream_refresh_server_info(server);
    if (result != 0) return result;
    if (stream_cancel_requested()) return -7;
    if (server->current_game != 0 && server->current_game != application_id) {
        vita_debug_log("[GameStream] app conflict selected=%d active=%d",
            application_id, server->current_game);
        /* Approval applies only to the app the user was warned about. /cancel
         * is host-wide and cannot atomically enforce this snapshot comparison. */
        if (quit_application_id != server->current_game) {
            return LEGACY_GAMESTREAM_APP_CONFLICT;
        }
        result = legacy_gamestream_quit_app(server);
        if (result != 0) return result;
    }
    if (stream_cancel_requested()) return -7;
    const int resume = server->current_game != 0;

    if (sceKernelGetRandomNumber(remote_key, 16) < 0 ||
        sceKernelGetRandomNumber(remote_iv, 4) < 0) return -3;
    memset(remote_iv + 4, 0, 12);

    if (make_hex(remote_key, 16, rikey_hex, sizeof(rikey_hex)) != 0) {
        return -3;
    }

    rikey_id =
        ((unsigned int)remote_iv[0] << 24) |
        ((unsigned int)remote_iv[1] << 16) |
        ((unsigned int)remote_iv[2] << 8) |
        (unsigned int)remote_iv[3];

    snprintf(
        query,
        sizeof(query),
        "appid=%d&mode=%dx%dx%d&additionalStates=1&sops=%d"
        "&rikey=%s&rikeyid=%u&localAudioPlayMode=%d"
        "&surroundAudioInfo=%u&remoteControllersBitmap=1&gcmap=1"
        "%s",
        application_id,
        settings->width,
        settings->height,
        settings->fps,
        settings->sops,
        rikey_hex,
        rikey_id,
        settings->localaudio,
        (unsigned int)SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(
            AUDIO_CONFIGURATION_STEREO),
        LiGetLaunchUrlQueryParameters());

    vita_debug_log("[GameStream] %s app=%d host=%s",
        resume ? "resume" : "launch", application_id, server->address);
#ifdef MOONLIGHT_HTTP_DISCONNECT_TEST
    disconnect_http_test_arm(application_id);
#endif
    result = legacy_gamestream_request(server, resume ? "resume" : "launch", query,
        20L, &body, &body_length);
    if (result != 0) return result;
    result = moonlight_xml_parse_launch(body, body_length, resume, &response);
    free(body);
    if (result != MOONLIGHT_XML_OK) {
        vita_debug_log("[GameStream] launch/resume rejected result=%d status=%d message=%s",
            result, response.status_code, response.status_message);
        return result;
    }

    if (response.session_url_present) {
        if (strlen(response.session_url) >= session_url_size) return -6;
        memcpy(session_url, response.session_url, strlen(response.session_url) + 1);
    } else {
        /* Older GFE/Sunshine may omit the URL; malformed/present URLs never reach this fallback. */
        int written = snprintf(session_url, session_url_size, "rtsp://%s:48010", server->address);
        if (written < 0 || (size_t)written >= session_url_size) return -6;
    }

    strncpy(
        server->rtsp_session_url,
        session_url,
        sizeof(server->rtsp_session_url) - 1);
    server->rtsp_session_url[
        sizeof(server->rtsp_session_url) - 1] = '\0';
    server->current_game = application_id;

    vita_debug_log("[GameStream] launch/resume accepted session=%s", session_url);
    return 0;
}

static int run_stream_session(
    LegacyGameStreamServer *server,
    int application_id,
    int quit_application_id,
    const MoonlightSettings *settings)
{
    char session_url[256];
    unsigned char remote_key[16];
    unsigned char remote_iv[16];
    SERVER_INFORMATION server_info;
    STREAM_CONFIGURATION stream_config;
    CONNECTION_LISTENER_CALLBACKS callbacks;
    int result;

    memset(&session_url, 0, sizeof(session_url));
    memset(remote_key, 0, sizeof(remote_key));
    memset(remote_iv, 0, sizeof(remote_iv));

    result = load_launch_session(
            server,
            application_id,
            quit_application_id,
            settings,
            session_url,
            sizeof(session_url),
            remote_key,
            remote_iv);
    if (result != 0) return result;

#if defined(MOONLIGHT_PENDING_DISCONNECT_TEST) && !defined(MOONLIGHT_HTTP_DISCONNECT_TEST)
    disconnect_test_pending_checkpoint(application_id);
#endif

    if (stream_cancel_requested()) {
        vita_debug_log("[Stream] launch cancelled before RTSP setup");
        return -7;
    }

    LiInitializeStreamConfiguration(&stream_config);
    stream_config.width = settings->width;
    stream_config.height = settings->height;
    stream_config.fps = settings->fps;
    stream_config.bitrate = settings->bitrate;
    stream_config.packetSize = 1024;
    stream_config.streamingRemotely =
        settings->enable_remote_stream_optimization
            ? STREAM_CFG_AUTO
            : STREAM_CFG_LOCAL;
    stream_config.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    stream_config.supportedVideoFormats = VIDEO_FORMAT_H264;
    stream_config.clientRefreshRateX100 = 6000;
    stream_config.colorSpace = COLORSPACE_REC_601;
    stream_config.colorRange = COLOR_RANGE_LIMITED;
    stream_config.encryptionFlags = ENCFLG_NONE;
    memcpy(stream_config.remoteInputAesKey, remote_key, sizeof(remote_key));
    memcpy(stream_config.remoteInputAesIv, remote_iv, sizeof(remote_iv));

    LiInitializeServerInformation(&server_info);
    server_info.address = server->address;
    server_info.serverInfoAppVersion =
        server->server_info_app_version[0]
            ? server->server_info_app_version
            : "7.1.431.0";
    server_info.serverInfoGfeVersion =
        server->server_info_gfe_version[0]
            ? server->server_info_gfe_version
            : NULL;
    server_info.rtspSessionUrl = server->rtsp_session_url;
    server_info.serverCodecModeSupport =
        server->server_codec_mode_support
            ? server->server_codec_mode_support
            : SCM_H264;

    if (stream_cancel_requested()) {
        vita_debug_log("[Stream] launch cancelled before LiStartConnection");
        return -7;
    }

    initialize_connection_callbacks(&callbacks);

    vita_debug_log(
        "[Stream] LiStartConnection %dx%d %dfps %dkbps server=%s codec=0x%08x",
        stream_config.width,
        stream_config.height,
        stream_config.fps,
        stream_config.bitrate,
        server_info.serverInfoAppVersion,
        server_info.serverCodecModeSupport);

    if (stream_cancel_requested()) {
        vita_debug_log("[Stream] launch cancelled before LiStartConnection");
        return -7;
    }

    result = LiStartConnection(
        &server_info,
        &stream_config,
        &callbacks,
        &decoder_callbacks_vita,
        &audio_callbacks_vita,
        NULL,
        0,
        NULL,
        0);

    if (result != 0) {
        vita_debug_log(
            "[Stream] LiStartConnection failed result=%d",
            result);
    }

    return finish_common_startup(result);
}

static void *stream_thread_main(void *arg)
{
    StreamThreadArgs *args = (StreamThreadArgs *)arg;
    int result;

    reset_recovery_wait_logs();
    vita_debug_log("[Stream] worker start app=%d", args->application_id);
    moonlight_log_memory_budget("stream-before-setup");
    moonlight_log_heap_trace("stream-before-setup");
    result = stream_cancel_requested() ? -7 : run_stream_session(
        args->server, args->application_id,
        args->quit_application_id, &args->settings);
    vita_debug_log("[Stream] setup finished app=%d result=%d", args->application_id, result);
    moonlight_log_memory_budget("stream-after-setup");
    moonlight_log_heap_trace("stream-after-setup");

    if (result != 0) {
        /* LiStartConnection unwinds partial setup itself; no active transport remains. */
        summarize_recovery_wait_logs();
        moonlight_stats_end(args->stats_generation);
        free(args);
        finish_stream_worker(MOONLIGHT_EVENT_STREAM_FAILED, result);
        return NULL;
    }

    uint64_t next_stats_us = 0;
    for (;;) {
        pthread_mutex_lock(&s_stream_mutex);
        int stop = s_stream_stop_requested || s_stream_cancel_requested;
        pthread_mutex_unlock(&s_stream_mutex);
        if (stop) {
            break;
        }
        if (args->settings.show_fps && LiGetMicroseconds() >= next_stats_us) {
            uint32_t rtt = 0, variation = 0;
            int valid = LiGetEstimatedRttInfo(&rtt, &variation);
            moonlight_stats_network(args->stats_generation, valid, rtt, variation);
            next_stats_us = LiGetMicroseconds() + 1000000ull;
        }
        sceKernelDelayThread(10000);
    }

    vita_debug_log("[Stream] stop worker start (session owner)");
    moonlight_stats_end(args->stats_generation);
    vita_input_stop();
    /* Never run on a Limelight/input thread, and never allocate a thread to reach here. */
    LiStopConnection();
    summarize_recovery_wait_logs();
    moonlight_log_memory_budget("stream-after-stop");
    moonlight_log_heap_trace("stream-after-stop");
    pthread_mutex_lock(&s_stream_mutex);
    result = s_stream_termination_result;
    pthread_mutex_unlock(&s_stream_mutex);
    free(args);
    vita_debug_log("[Stream] stop worker finished result=%d", result);
    finish_stream_worker(MOONLIGHT_EVENT_STREAM_STOPPED, result);
    return NULL;
}

}

extern "C" int moonlight_stream_start(
    LegacyGameStreamServer *server,
    const char *unique_path,
    int application_id,
    int quit_application_id,
    const MoonlightSettings *settings,
    MoonlightStreamEventCallback callback,
    void *userdata)
{
    StreamThreadArgs *args;
    pthread_t stream_thread;
    int result;

    if (!server || !settings || !server->curl || !server->address[0] || !unique_path) {
        return -1;
    }
    args = (StreamThreadArgs *)malloc(sizeof(*args));
    if (!args) {
        return -3;
    }
    pthread_mutex_lock(&s_stream_mutex);
    if (s_stream_thread_running) {
        pthread_mutex_unlock(&s_stream_mutex);
        free(args);
        vita_debug_log("[Stream] start rejected: session owner already running");
        return -2;
    }
    s_stream_thread_running = 1;
    s_stream_stop_requested = 0;
    s_stream_cancel_requested = 0;
    s_stream_termination_result = 0;
    s_common_starting = 0;
    s_start_interrupt_sent = 0;
    s_stream_context.callback = callback;
    s_stream_context.userdata = userdata;
    s_stream_context.started = 0;
    s_controller_light.generation = s_controller_light.generation + 1u;
    if (!s_controller_light.generation) s_controller_light.generation = 1;
    s_controller_light.mode = settings->controller_light;
    if (s_controller_light.mode < MOONLIGHT_CONTROLLER_LIGHT_OFF ||
        s_controller_light.mode > MOONLIGHT_CONTROLLER_LIGHT_DUALSENSE) {
        s_controller_light.mode = MOONLIGHT_CONTROLLER_LIGHT_OFF;
    }
    s_controller_light.r = 0;
    s_controller_light.g = 0;
    s_controller_light.b = 0;
    pthread_mutex_unlock(&s_stream_mutex);

    args->server = server;
    args->application_id = application_id;
    args->quit_application_id = quit_application_id;
    args->settings = *settings;
    args->stats_generation = moonlight_stats_begin(settings);
    pthread_mutex_lock(&s_stream_mutex);
    s_stream_stats_generation = args->stats_generation;
    pthread_mutex_unlock(&s_stream_mutex);

    vita_input_configure(
        settings->controller_type,
        settings->swap_shoulder_buttons,
        settings->enable_psbutton_capture,
        settings->touchscreen_mode,
        settings->mouse_acceleration,
        settings->enable_motion_controls,
        settings->back_deadzone_top,
        settings->back_deadzone_right,
        settings->back_deadzone_bottom,
        settings->back_deadzone_left);
    vita_input_set_runtime(
        settings->jp_layout,
        settings->enable_front_touchzones,
        settings->enable_double_tap_sprint,
        settings->double_tap_sprint_step_time,
        settings->keyboard_layout,
        settings->mapping_enabled);
    moonlight_video_set_stream_options(
        settings->fps,
        settings->enable_frame_pacer,
        settings->enable_vita_vblank_wait,
        settings->center_region_only,
        settings->enable_ref_frame_invalidation);

    result = pthread_create(&stream_thread, NULL, stream_thread_main, args);
    if (result != 0) {
        moonlight_stats_end(args->stats_generation);
        free(args);
        pthread_mutex_lock(&s_stream_mutex);
        s_stream_context.callback = NULL;
        s_stream_context.userdata = NULL;
        s_stream_thread_running = 0;
        pthread_mutex_unlock(&s_stream_mutex);
        vita_debug_log("[Stream] session owner create failed=%d", result);
        return -4;
    }
    pthread_detach(stream_thread);
    vita_debug_log("[Stream] worker launched for app=%d", application_id);
    return 0;
}

extern "C" int moonlight_stream_stop(void)
{
    pthread_mutex_lock(&s_stream_mutex);
    if (!s_stream_thread_running) {
        pthread_mutex_unlock(&s_stream_mutex);
        vita_debug_log("[Stream] stop ignored: no active or pending session");
        return 0;
    }
    int already_requested = s_stream_stop_requested;
    s_stream_cancel_requested = 1;
    s_stream_stop_requested = 1;
    interrupt_common_startup_locked();
    pthread_mutex_unlock(&s_stream_mutex);
    if (!already_requested) {
        vita_debug_log("[Stream] stop requested for session owner");
    }
    return 0;
}

extern "C" int moonlight_stream_is_running(void)
{
    pthread_mutex_lock(&s_stream_mutex);
    int running = s_stream_thread_running;
    pthread_mutex_unlock(&s_stream_mutex);
    return running;
}

extern "C" int moonlight_stream_get_stats(MoonlightStreamStats *out)
{
    /* UI only reads cached counters; it never waits on the ENet mutex. */
    return moonlight_stats_snapshot(out);
}

extern "C" int moonlight_stream_get_controller_light(MoonlightControllerLight *out)
{
    if (!out) return -1;
    pthread_mutex_lock(&s_stream_mutex);
    *out = s_controller_light;
    if (!s_stream_thread_running || !s_stream_context.started ||
        s_stream_stop_requested || s_stream_cancel_requested) {
        out->mode = MOONLIGHT_CONTROLLER_LIGHT_OFF;
    }
    pthread_mutex_unlock(&s_stream_mutex);
    return 0;
}
