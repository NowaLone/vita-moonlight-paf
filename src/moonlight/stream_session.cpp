#include "moonlight/stream_session.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <psp2/kernel/rng.h>
#include <pthread.h>

#include <Limelight.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "vita_video_renderer.h"

namespace {

struct LaunchBuffer {
    char *memory;
    size_t size;
};

struct StreamContext {
    MoonlightStreamEventCallback callback;
    void *userdata;
    int started;
};

struct StreamThreadArgs {
    LegacyGameStreamServer *server;
    char unique_path[512];
    int application_id;
    MoonlightSettings settings;
};

static StreamContext s_stream_context = { NULL, NULL, 0 };
static pthread_t s_stream_thread;
static volatile int s_stream_thread_running = 0;

static size_t write_response(void *contents, size_t size, size_t count, void *userdata)
{
    LaunchBuffer *buffer = (LaunchBuffer *)userdata;
    size_t length = size * count;
    char *memory = (char *)realloc(
        buffer->memory,
        buffer->size + length + 1);

    if (!memory) {
        return 0;
    }

    buffer->memory = memory;
    memcpy(buffer->memory + buffer->size, contents, length);
    buffer->size += length;
    buffer->memory[buffer->size] = '\0';
    return length;
}

static void emit_event(
    MoonlightEventType type,
    int result)
{
    if (s_stream_context.callback) {
        s_stream_context.callback(
            type,
            result,
            s_stream_context.userdata);
    }
}

static void on_stage_start(int stage)
{
    vita_debug_log(
        "[Stream] stage start %d (%s)",
        stage,
        LiGetStageName(stage));
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
    s_stream_context.started = 1;
    vita_debug_log("[Stream] connection started");
    emit_event(MOONLIGHT_EVENT_STREAM_STARTED, 0);
}

static void on_connection_terminated(int result)
{
    vita_debug_log(
        "[Stream] connection terminated result=%d",
        result);

    if (s_stream_context.started) {
        emit_event(MOONLIGHT_EVENT_STREAM_STOPPED, result);
    }
    s_stream_context.started = 0;
}

static void on_status_update(int status)
{
    vita_debug_log("[Stream] connection status=%d", status);
}

static void on_log_message(const char *format, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    vita_debug_log("[Limelight] %s", buffer);
}

static void on_hdr_mode(bool enabled)
{
    vita_debug_log("[Stream] HDR mode=%d", enabled ? 1 : 0);
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
    const char *unique_path,
    int application_id,
    const MoonlightSettings *settings,
    char *session_url,
    size_t session_url_size,
    unsigned char remote_key[16],
    unsigned char remote_iv[16])
{
    char unique_id[32];
    char uuid[40];
    char rikey_hex[33];
    char url[4096];
    unsigned char uuid_bytes[16];
    unsigned int rikey_id;
    LaunchBuffer response;
    FILE *file;
    CURLcode curl_result;
    const char *session;
    const char *session_end;
    size_t session_length;
    int accepted;

    if (!server || !server->curl || !unique_path || !settings ||
        !session_url || session_url_size == 0) {
        return -1;
    }

    memset(unique_id, 0, sizeof(unique_id));
    file = fopen(unique_path, "rb");
    if (!file || fread(unique_id, 1, 16, file) != 16) {
        if (file) fclose(file);
        return -2;
    }
    fclose(file);
    unique_id[16] = '\0';

    if (sceKernelGetRandomNumber(uuid_bytes, sizeof(uuid_bytes)) < 0 ||
        sceKernelGetRandomNumber(remote_key, 16) < 0 ||
        sceKernelGetRandomNumber(remote_iv, 4) < 0) {
        return -3;
    }
    memset(remote_iv + 4, 0, 12);

    snprintf(
        uuid,
        sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x",
        uuid_bytes[0], uuid_bytes[1], uuid_bytes[2], uuid_bytes[3],
        uuid_bytes[4], uuid_bytes[5], uuid_bytes[6], uuid_bytes[7],
        uuid_bytes[8], uuid_bytes[9], uuid_bytes[10], uuid_bytes[11],
        uuid_bytes[12], uuid_bytes[13], uuid_bytes[14], uuid_bytes[15]);

    if (make_hex(remote_key, 16, rikey_hex, sizeof(rikey_hex)) != 0) {
        return -3;
    }

    rikey_id =
        ((unsigned int)remote_iv[0] << 24) |
        ((unsigned int)remote_iv[1] << 16) |
        ((unsigned int)remote_iv[2] << 8) |
        (unsigned int)remote_iv[3];

    snprintf(
        url,
        sizeof(url),
        "https://%s:%u/%s?uniqueid=%s&uuid=%s&appid=%d"
        "&mode=%dx%dx%d&additionalStates=1&sops=%d"
        "&rikey=%s&rikeyid=%u&localAudioPlayMode=%d"
        "&surroundAudioInfo=%u&remoteControllersBitmap=1&gcmap=1"
        "%s",
        server->address,
        server->https_port ? server->https_port : 47984,
        server->current_game ? "resume" : "launch",
        unique_id,
        uuid,
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

    response.memory = (char *)malloc(1);
    response.size = 0;
    if (!response.memory) {
        return -4;
    }
    response.memory[0] = '\0';

    curl_easy_setopt((CURL *)server->curl, CURLOPT_URL, url);
    curl_easy_setopt(
        (CURL *)server->curl,
        CURLOPT_WRITEFUNCTION,
        write_response);
    curl_easy_setopt(
        (CURL *)server->curl,
        CURLOPT_WRITEDATA,
        &response);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_TIMEOUT, 20L);

    vita_debug_log(
        "[GameStream] launching app %d on %s",
        application_id,
        server->address);

    curl_result = curl_easy_perform((CURL *)server->curl);
    if (curl_result != CURLE_OK) {
        vita_debug_log(
            "[GameStream] launch curl failed: %s",
            curl_easy_strerror(curl_result));
        free(response.memory);
        return -5;
    }

    accepted = strstr(response.memory, "status_code=\"200\"") != NULL;
    if (!accepted &&
        !strstr(response.memory, "<gamesession>1</gamesession>") &&
        !strstr(response.memory, "<resume>1</resume>")) {
        vita_debug_log(
            "[GameStream] launch rejected: %.180s",
            response.memory);
        free(response.memory);
        return -6;
    }

    session_url[0] = '\0';
    session = strstr(response.memory, "<sessionUrl0>");
    if (session) {
        session += strlen("<sessionUrl0>");
        session_end = strstr(session, "</sessionUrl0>");
        session_length = session_end
            ? (size_t)(session_end - session)
            : 0;
        if (session_length >= session_url_size) {
            session_length = session_url_size - 1;
        }
        memcpy(session_url, session, session_length);
        session_url[session_length] = '\0';
    }

    if (!session_url[0]) {
        snprintf(
            session_url,
            session_url_size,
            "rtsp://%s:48010",
            server->address);
    }

    strncpy(
        server->rtsp_session_url,
        session_url,
        sizeof(server->rtsp_session_url) - 1);
    server->rtsp_session_url[
        sizeof(server->rtsp_session_url) - 1] = '\0';
    server->current_game = application_id;

    vita_debug_log(
        "[GameStream] launch %s session %s",
        accepted ? "ok" : "busy",
        session_url);

    free(response.memory);
    return 0;
}

static int run_stream_session(
    LegacyGameStreamServer *server,
    const char *unique_path,
    int application_id,
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

    if (load_launch_session(
            server,
            unique_path,
            application_id,
            settings,
            session_url,
            sizeof(session_url),
            remote_key,
            remote_iv) != 0) {
        return -1;
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

    initialize_connection_callbacks(&callbacks);

    vita_debug_log(
        "[Stream] LiStartConnection %dx%d %dfps %dkbps server=%s codec=0x%08x",
        stream_config.width,
        stream_config.height,
        stream_config.fps,
        stream_config.bitrate,
        server_info.serverInfoAppVersion,
        server_info.serverCodecModeSupport);

    result = LiStartConnection(
        &server_info,
        &stream_config,
        &callbacks,
        &decoder_callbacks_vita,
        NULL,
        NULL,
        0,
        NULL,
        0);

    if (result != 0) {
        vita_debug_log(
            "[Stream] LiStartConnection failed result=%d",
            result);
    }

    return result;
}

static void *stream_thread_main(void *arg)
{
    StreamThreadArgs *args = (StreamThreadArgs *)arg;
    int result;

    vita_debug_log(
        "[Stream] worker start app=%d",
        args->application_id);

    result = run_stream_session(
        args->server,
        args->unique_path,
        args->application_id,
        &args->settings);

    vita_debug_log(
        "[Stream] worker finished app=%d result=%d",
        args->application_id,
        result);

    if (result != 0 && !s_stream_context.started) {
        emit_event(MOONLIGHT_EVENT_STREAM_FAILED, result);
    }

    free(args);
    s_stream_thread_running = 0;
    return NULL;
}

}

extern "C" int moonlight_stream_start(
    LegacyGameStreamServer *server,
    const char *unique_path,
    int application_id,
    const MoonlightSettings *settings,
    MoonlightStreamEventCallback callback,
    void *userdata)
{
    StreamThreadArgs *args;
    int result;

    if (!server || !settings || !server->curl || !server->address[0] ||
        !unique_path) {
        return -1;
    }

    if (s_stream_thread_running) {
        vita_debug_log("[Stream] start rejected: worker already running");
        return -2;
    }

    args = (StreamThreadArgs *)malloc(sizeof(*args));
    if (!args) {
        return -3;
    }

    args->server = server;
    strncpy(args->unique_path, unique_path, sizeof(args->unique_path) - 1);
    args->unique_path[sizeof(args->unique_path) - 1] = '\0';
    args->application_id = application_id;
    args->settings = *settings;

    s_stream_context.callback = callback;
    s_stream_context.userdata = userdata;
    s_stream_context.started = 0;

    s_stream_thread_running = 1;
    result = pthread_create(
        &s_stream_thread,
        NULL,
        stream_thread_main,
        args);
    if (result != 0) {
        s_stream_thread_running = 0;
        free(args);
        vita_debug_log(
            "[Stream] pthread_create failed result=%d",
            result);
        s_stream_context.callback = NULL;
        s_stream_context.userdata = NULL;
        return -4;
    }

    pthread_detach(s_stream_thread);

    vita_debug_log(
        "[Stream] worker launched for app=%d",
        application_id);

    return 0;
}

extern "C" int moonlight_stream_stop(void)
{
    LiStopConnection();
    return 0;
}
