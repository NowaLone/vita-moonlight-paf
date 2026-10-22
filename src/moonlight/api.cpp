#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>

#include "moonlight/api.h"
#include "moonlight/gamestream_xml.h"
#include "moonlight/settings.h"
#include "legacy_host_discovery.h"
#include "backend/legacy_device_store.h"
#include "legacy_gamestream.h"
#include "moonlight/stream_session.h"
#include "config.h"
#include "debug.h"

namespace {

static MoonlightEventCallback s_callback = NULL;
static void *s_userdata = NULL;
static MoonlightHost s_current_host = {0};
static bool s_has_current_host = false;
static MoonlightConnectionState s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
static LegacyGameStreamServer s_server;
static bool s_game_stream_initialized = false;
static MoonlightApplication s_applications[MOONLIGHT_MAX_APPLICATIONS];
static int s_application_count = 0;
static bool s_applications_valid = false;
static int s_stream_application_id = -1;
static bool s_stream_active = false;
static bool s_stream_pending = false;
static bool s_disconnect_pending = false;
static bool s_disconnect_worker_running = false;
static pthread_mutex_t s_host_control_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool disconnect_pending()
{
    /* Duplicate disconnect requests must be observable even while cleanup owns the mutex. */
    return __atomic_load_n(&s_disconnect_pending, __ATOMIC_ACQUIRE);
}

static void set_disconnect_pending(bool pending)
{
    __atomic_store_n(&s_disconnect_pending, pending, __ATOMIC_RELEASE);
}

static bool try_begin_host_request(bool for_disconnect)
{
    if (pthread_mutex_trylock(&s_host_control_mutex) != 0) return false;
    if (!for_disconnect && disconnect_pending()) {
        pthread_mutex_unlock(&s_host_control_mutex);
        return false;
    }
    return true;
}

class HostControlRequest {
public:
    explicit HostControlRequest(bool for_disconnect)
        : m_acquired(try_begin_host_request(for_disconnect)) {}
    ~HostControlRequest()
    {
        if (m_acquired) pthread_mutex_unlock(&s_host_control_mutex);
    }
    bool Acquired() const { return m_acquired; }

private:
    HostControlRequest(const HostControlRequest &);
    HostControlRequest &operator=(const HostControlRequest &);
    bool m_acquired;
};

static void invalidate_applications()
{
    s_applications_valid = false;
    s_application_count = 0;
    memset(s_applications, 0, sizeof(s_applications));
}

static void emit(MoonlightEventType type,
                 int result,
                 int host_id,
                 int application_id,
                 const char *address,
                 int server_status_code = 0,
                 const char *server_status_message = NULL)
{
    if (!s_callback) return;

    MoonlightEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.result = result;
    event.host_id = host_id;
    event.application_id = application_id;
    event.address = address;
    event.server_status_code = server_status_code;
    if (server_status_message) {
        strncpy(event.server_status_message, server_status_message,
                sizeof(event.server_status_message) - 1);
    }
    s_callback(&event, s_userdata);
}

static void emit_pairing_required(const char pin[5])
{
    if (!s_callback) return;

    MoonlightEvent event;
    memset(&event, 0, sizeof(event));
    event.type = MOONLIGHT_EVENT_PAIRING_REQUIRED;
    event.result = 0;
    event.host_id = s_current_host.id;
    event.application_id = -1;
    event.address = s_current_host.internal;
    memcpy(event.pairing_pin, pin, 5);
    s_callback(&event, s_userdata);
}

static void on_discovery_event(
    LegacyHostDiscoveryEventType type,
    const MoonlightHost *host,
    void *userdata)
{
    (void)userdata;

    if (type == LEGACY_HOST_DISCOVERY_FOUND) {
        if (!host) return;

        emit(
            MOONLIGHT_EVENT_HOSTS_CHANGED,
            0,
            host->id,
            -1,
            host->internal
        );
        return;
    }

    if (type == LEGACY_HOST_DISCOVERY_FINISHED) {
        emit(
            MOONLIGHT_EVENT_HOST_SCAN_FINISHED,
            0,
            -1,
            -1,
            NULL
        );
    }
}

static bool is_pairing_network_error(int error_code)
{
    switch ((CURLcode)error_code) {
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_SSL_CONNECT_ERROR:
        return true;
    default:
        return false;
    }
}

static int classify_pairing_error(int legacy_result)
{
    if (legacy_result == LEGACY_GAMESTREAM_OK) {
        return MOONLIGHT_PAIRING_SUCCESS;
    }

    if (legacy_result == LEGACY_GAMESTREAM_INCORRECT_PIN) {
        return MOONLIGHT_PAIRING_ERROR_INCORRECT_PIN;
    }

    if (legacy_result == LEGACY_GAMESTREAM_SECURITY_ERROR) {
        return MOONLIGHT_PAIRING_ERROR_SECURITY;
    }

    if (legacy_result == LEGACY_GAMESTREAM_OUT_OF_MEMORY ||
        legacy_result == LEGACY_GAMESTREAM_INTERNAL_ERROR ||
        legacy_gamestream_last_http_error() == CURLE_OUT_OF_MEMORY) {
        return MOONLIGHT_PAIRING_ERROR_INTERNAL;
    }

    if (is_pairing_network_error(legacy_gamestream_last_http_error())) {
        return MOONLIGHT_PAIRING_ERROR_NETWORK;
    }

    /*
     * Reaching this point means the request got far enough to produce a
     * protocol or pairing-validation error. It is not proof of a wrong PIN.
     */
    return MOONLIGHT_PAIRING_ERROR_PROTOCOL;
}

static void update_host_from_server()
{
    s_current_host.paired = s_server.paired ? 1 : 0;

    if (s_server.mac[0]) {
        strncpy(s_current_host.mac, s_server.mac, sizeof(s_current_host.mac) - 1);
        s_current_host.mac[sizeof(s_current_host.mac) - 1] = '\0';
    }
}

static void on_stream_event(
    MoonlightEventType type,
    int result,
    void *userdata)
{
    (void)userdata;

    if (type == MOONLIGHT_EVENT_STREAM_STARTED) {
        s_stream_pending = false;
        s_stream_active = true;
        s_connection_state = MOONLIGHT_CONNECTION_STREAMING;
    } else if (type == MOONLIGHT_EVENT_STREAM_STOPPED) {
        s_stream_pending = false;
        s_stream_active = false;
        s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    } else if (type == MOONLIGHT_EVENT_STREAM_FAILED) {
        s_stream_pending = false;
        s_stream_active = false;
        s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    }

    emit(
        type,
        result,
        s_current_host.id,
        s_stream_application_id,
        s_current_host.internal);

    if (type == MOONLIGHT_EVENT_STREAM_STOPPED ||
        type == MOONLIGHT_EVENT_STREAM_FAILED) {
        s_stream_application_id = -1;
    }
}


static void *disconnect_cleanup_worker(void *)
{
    /* The request releases this mutex after scheduling us. New callers only trylock;
     * they never wait on the main thread for network/transport cleanup. */
    pthread_mutex_lock(&s_host_control_mutex);
    while (moonlight_stream_is_running()) {
        sceKernelDelayThread(10000);
    }

    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }

    s_stream_pending = false;
    s_stream_active = false;
    s_stream_application_id = -1;
    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;

    emit(MOONLIGHT_EVENT_CONNECTION_CLOSED, 0, s_current_host.id, -1, s_current_host.internal);
    s_disconnect_worker_running = false;
    set_disconnect_pending(false);
    pthread_mutex_unlock(&s_host_control_mutex);
    return NULL;
}

static int begin_disconnect_cleanup(void)
{
    pthread_t worker;
    int result;

    if (s_disconnect_worker_running) return 0;
    s_disconnect_worker_running = true;
    result = pthread_create(&worker, NULL, disconnect_cleanup_worker, NULL);
    if (result != 0) {
        s_disconnect_worker_running = false;
        vita_debug_log("[GameStream] disconnect cleanup worker create failed=%d", result);
        return result;
    }
    /* Publish accepted cleanup only after creation succeeds. The worker cannot
     * acquire the control mutex until this request releases admission. */
    set_disconnect_pending(true);
    pthread_detach(worker);
    return 0;
}

}

int moonlight_api_init(void)
{
    invalidate_applications();
    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
    s_game_stream_initialized = false;
    memset(&s_server, 0, sizeof(s_server));

    return legacy_host_discovery_init(on_discovery_event, NULL);
}

void moonlight_api_shutdown(void)
{
    /* Shutdown may wait, unlike normal host requests. Wait outside the mutex when
     * a scheduled cleanup worker still needs to acquire it. */
    for (;;) {
        pthread_mutex_lock(&s_host_control_mutex);
        if (!s_disconnect_worker_running) break;
        pthread_mutex_unlock(&s_host_control_mutex);
        sceKernelDelayThread(10000);
    }

    if (s_stream_active || s_stream_pending || moonlight_stream_is_running()) {
        moonlight_stream_stop();
        while (moonlight_stream_is_running()) sceKernelDelayThread(10000);
    }
    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }
    legacy_host_discovery_shutdown();
    invalidate_applications();
    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
    s_stream_active = false;
    s_stream_pending = false;
    s_stream_application_id = -1;
    set_disconnect_pending(false);
    s_disconnect_worker_running = false;
    memset(&s_server, 0, sizeof(s_server));
    memset(&s_current_host, 0, sizeof(s_current_host));
    s_callback = NULL;
    s_userdata = NULL;
    pthread_mutex_unlock(&s_host_control_mutex);
}

int moonlight_api_open_settings(void)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_open();
}

int moonlight_api_get_settings(MoonlightSettings *out)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_get_all(out);
}

int moonlight_api_get_setting_value(MoonlightSettingKey key, int *out_value)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_get_value(key, out_value);
}

int moonlight_api_set_setting_value(MoonlightSettingKey key, int value)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_set_value(key, value);
}

int moonlight_api_set_event_callback(MoonlightEventCallback callback, void *userdata)
{
    s_callback = callback;
    s_userdata = userdata;
    return 0;
}

int moonlight_api_get_hosts(MoonlightHost *out, int capacity)
{
    return legacy_device_store_get_hosts(out, capacity);
}

int moonlight_api_get_discovered_hosts(MoonlightHost *out, int capacity)
{
    return legacy_host_discovery_get_hosts(out, capacity);
}

int moonlight_api_search_hosts(void)
{
    if (legacy_host_discovery_is_running()) return 0;

    /* Preserve attempt-before-worker event order. Start errors are synchronous:
     * Search already displays the returned error, so do not queue it a second time. */
    emit(MOONLIGHT_EVENT_HOST_SCAN_STARTED, 0, -1, -1, NULL);
    return legacy_host_discovery_start();
}

int moonlight_api_stop_host_search(void)
{
    return legacy_host_discovery_stop();
}

int moonlight_api_add_host(const char *address, uint16_t port, const char *name)
{
    return legacy_device_store_add_host(address, port, name);
}

int moonlight_api_connect_host(const MoonlightHost *host)
{
    HostControlRequest request(false);
    if (!request.Acquired()) return -1;
    char client_directory[512];
    char server_certificate_path[512] = {0};

    if (!host || !host->internal[0] || disconnect_pending() ||
        s_disconnect_worker_running || moonlight_stream_is_running()) {
        return -1;
    }

    if (legacy_device_store_get_client_directory(
            client_directory, sizeof(client_directory)) != 0) {
        emit(
            MOONLIGHT_EVENT_CONNECTION_FAILED,
            -1,
            host->id,
            -1,
            host->internal);
        return -1;
    }

    invalidate_applications();

    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }

    /* Resolve the selected stored identity before any unauthenticated serverinfo response. */
    legacy_device_store_get_server_certificate_path(host, server_certificate_path,
        sizeof(server_certificate_path));
    int result = legacy_gamestream_init(
        &s_server,
        host->internal,
        host->port,
        client_directory,
        server_certificate_path,
        0,
        true);

    if (result == LEGACY_GAMESTREAM_OK && s_server.unique_id[0]) {
        if (legacy_device_store_set_host_id(host, s_server.unique_id) != 0) {
            vita_debug_log(
                "[GameStream] failed to persist server uniqueid=%s",
                s_server.unique_id);
        }
        /* Manual hosts are saved only after connection; assign the future pairing pin destination. */
        if (!s_server.server_certificate_path[0] &&
            legacy_device_store_get_server_certificate_path(host, server_certificate_path,
                sizeof(server_certificate_path)) == 0) {
            legacy_gamestream_set_certificate_path(&s_server, server_certificate_path);
        }
    }

    if (result != LEGACY_GAMESTREAM_OK) {
        vita_debug_log(
            "[GameStream] init failed for %s: %d (%s)",
            host->internal,
            result,
            legacy_gamestream_error());

        s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
        s_has_current_host = false;

        emit(
            MOONLIGHT_EVENT_CONNECTION_FAILED,
            result,
            host->id,
            -1,
            host->internal);
        return result;
    }

    s_game_stream_initialized = true;
    s_current_host = *host;
    if (s_server.unique_id[0]) {
        strncpy(
            s_current_host.host_id,
            s_server.unique_id,
            sizeof(s_current_host.host_id) - 1);
        s_current_host.host_id[sizeof(s_current_host.host_id) - 1] = '\0';
    }
    update_host_from_server();
    s_has_current_host = true;

    s_connection_state = s_server.paired
        ? MOONLIGHT_CONNECTION_PAIRED
        : MOONLIGHT_CONNECTION_READY;

    emit(
        MOONLIGHT_EVENT_CONNECTION_READY,
        0,
        s_current_host.id,
        -1,
        s_current_host.internal);

    return 0;
}

int moonlight_api_prepare_pairing(char out_pin[5])
{
    HostControlRequest request(false);
    if (!request.Acquired()) return -1;
    uint32_t random_value;

    if (!out_pin || !s_has_current_host ||
        s_connection_state != MOONLIGHT_CONNECTION_READY ||
        !s_game_stream_initialized) {
        return -1;
    }

    if (s_server.paired) {
        s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
        return LEGACY_GAMESTREAM_WRONG_STATE;
    }

    if (sceKernelGetRandomNumber(&random_value, sizeof(random_value)) < 0) {
        return -1;
    }

    snprintf(out_pin, 5, "%04u", (unsigned)(random_value % 10000));

    emit_pairing_required(out_pin);
    return 0;
}

int moonlight_api_pair_current_host(const char pin[5], const MoonlightCancellation *cancel)
{
    HostControlRequest request(false);
    if (!request.Acquired()) return -1;
    int result;
    LegacyGameStreamPairingError server_error = {0};

    if (!pin || !s_has_current_host ||
        s_connection_state != MOONLIGHT_CONNECTION_READY ||
        !s_game_stream_initialized) {
        return -1;
    }

    if (moonlight_is_cancelled(cancel)) return MOONLIGHT_PAIRING_CANCELLED;
    result = legacy_gamestream_pair(&s_server, pin, &server_error, cancel);
    if (moonlight_is_cancelled(cancel) || result == LEGACY_GAMESTREAM_CANCELLED) {
        vita_debug_log("[GameStream] pairing cancelled locally; no completion event");
        return MOONLIGHT_PAIRING_CANCELLED;
    }
    if (disconnect_pending()) return -1;
    if (result != LEGACY_GAMESTREAM_OK) {
        int pairing_result = classify_pairing_error(result);

        vita_debug_log(
            "[GameStream] pairing failed for %s: legacy=%d result=%d server_status=%d (%s)",
            s_current_host.internal,
            result,
            pairing_result,
            server_error.status_code,
            legacy_gamestream_error());

        emit(
            MOONLIGHT_EVENT_PAIRING_FAILED,
            pairing_result,
            s_current_host.id,
            -1,
            s_current_host.internal,
            server_error.status_code,
            server_error.status_message);
        return pairing_result;
    }

    update_host_from_server();
    s_connection_state = MOONLIGHT_CONNECTION_PAIRED;

    int save_result = legacy_device_store_mark_paired(&s_current_host);
    if (save_result != 0) {
        vita_debug_log(
            "[GameStream] pairing succeeded but device persistence failed: %d",
            save_result);
    }

    /*
     * Pairing succeeded even when saving the host metadata fails. Persistence
     * is separate from the protocol result and must not become a fake pairing
     * failure in the UI.
     */
    emit(
        MOONLIGHT_EVENT_PAIRING_FINISHED,
        MOONLIGHT_PAIRING_SUCCESS,
        s_current_host.id,
        -1,
        s_current_host.internal);

    return 0;
}

int moonlight_api_get_applications(MoonlightApplication *out, int capacity)
{
    HostControlRequest request(false);
    if (!request.Acquired()) return -1;
    if (out && capacity > 0) {
        if (!s_applications_valid) return -1;
        int count = s_application_count < capacity ? s_application_count : capacity;
        if (count > 0) memcpy(out, s_applications, sizeof(*out) * count);
        return count;
    }

    if (!s_has_current_host || !s_game_stream_initialized ||
        disconnect_pending() || s_stream_pending || moonlight_stream_is_running() ||
        s_connection_state != MOONLIGHT_CONNECTION_PAIRED) {
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, -1, s_current_host.id, -1, s_current_host.internal);
        return -1;
    }

    invalidate_applications();
    char *body = NULL;
    size_t length = 0;
    int result = legacy_gamestream_request(&s_server, "applist", NULL, 20L, &body, &length);
    MoonlightGameStreamResponse response;
    memset(&response, 0, sizeof(response));
    if (result == 0) {
        result = moonlight_xml_parse_applications(body, length, s_applications,
            MOONLIGHT_MAX_APPLICATIONS, &s_application_count, &response);
    }
    free(body);
    if (disconnect_pending() || s_connection_state != MOONLIGHT_CONNECTION_PAIRED) result = -1;
    if (result != 0) {
        invalidate_applications();
        vita_debug_log("[GameStream] applist failed result=%d status=%d message=%s",
            result, response.status_code, response.status_message);
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, result, s_current_host.id, -1, s_current_host.internal);
        return result;
    }
    s_applications_valid = true;
    vita_debug_log("[GameStream] applist count=%d total=%d", s_application_count,
        response.total_applications);
    emit(MOONLIGHT_EVENT_APPLICATIONS_READY, s_application_count, s_current_host.id, -1, s_current_host.internal);
    return s_application_count;
}

extern "C" int moonlight_launch_request(
    void *curl,
    const char *address,
    unsigned short https_port,
    const char *unique_path,
    int app_id,
    char *session_url,
    size_t session_url_size);

int moonlight_api_start_application(int application_id)
{
    HostControlRequest request(false);
    if (!request.Acquired()) return -1;
    vita_debug_log(
        "[GameStream] moonlight_api_start_application app=%d state=%d has_host=%d initialized=%d",
        application_id,
        (int)s_connection_state,
        s_has_current_host ? 1 : 0,
        s_game_stream_initialized ? 1 : 0);

    char unique_path[512];
    int result;
    MoonlightSettings settings;

    if (!s_has_current_host || !s_game_stream_initialized || !s_server.curl ||
        s_connection_state != MOONLIGHT_CONNECTION_PAIRED || s_stream_pending || s_stream_active) {
        vita_debug_log(
            "[GameStream] start rejected: host=%d initialized=%d curl=%d state=%d",
            s_has_current_host ? 1 : 0,
            s_game_stream_initialized ? 1 : 0,
            s_server.curl ? 1 : 0,
            (int)s_connection_state);
        return -1;
    }

    if (moonlight_settings_init() != 0) {
        vita_debug_log("[GameStream] settings init failed");
        return -2;
    }

    if (moonlight_settings_get_all(&settings) != 0) {
        vita_debug_log("[GameStream] settings get_all failed");
        return -2;
    }

    if (legacy_device_store_get_client_unique_id_path(
            unique_path,
            sizeof(unique_path)) != 0) {
        return -1;
    }

    vita_debug_log(
        "[GameStream] selected stream mode=%dx%dx%d bitrate=%d sops=%d",
        settings.width,
        settings.height,
        settings.fps,
        settings.bitrate,
        settings.sops);

    s_stream_application_id = application_id;
    s_stream_pending = true;

    vita_debug_log(
        "[GameStream] starting stream worker app=%d unique=%s",
        application_id,
        unique_path);

    result = moonlight_stream_start(
        &s_server,
        unique_path,
        application_id,
        &settings,
        on_stream_event,
        NULL);

    vita_debug_log(
        "[GameStream] moonlight_stream_start returned %d app=%d",
        result,
        application_id);

    if (result != 0) {
        s_stream_pending = false;
        s_stream_application_id = -1;
    }

    if (result != 0) {
        vita_debug_log(
            "[GameStream] stream failed for app %d: %d",
            application_id,
            result);
        if (!s_stream_active) {
            s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
        }
        return result;
    }

    return 0;
}

int moonlight_api_stop_application(void)
{
    HostControlRequest request(false);
    if (!request.Acquired()) return -1;
    if (!s_has_current_host || (!s_stream_active && !s_stream_pending)) return -1;
    return moonlight_stream_stop();
}

int moonlight_api_disconnect_host(void)
{
    HostControlRequest request(true);
    if (!request.Acquired()) return disconnect_pending() ? 0 : -1;
    int result;
    if (!s_has_current_host) return 0;
    if (disconnect_pending()) return 0;

    invalidate_applications();
    if (s_stream_active || s_stream_pending || moonlight_stream_is_running()) {
        result = moonlight_stream_stop();
        if (result != 0) {
            set_disconnect_pending(false);
            return result;
        }
    }
    result = begin_disconnect_cleanup();
    if (result != 0) {
        set_disconnect_pending(false);
        return result;
    }
    return 0;
}

MoonlightConnectionState moonlight_api_get_connection_state(void)
{
    return s_connection_state;
}

int moonlight_api_get_stream_stats(MoonlightStreamStats *out)
{
    return moonlight_stream_get_stats(out);
}
