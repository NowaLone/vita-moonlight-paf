#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/rng.h>

#include "moonlight/api.h"
#include "moonlight/settings.h"
#include "legacy_host_discovery.h"
#include "backend/legacy_device_store.h"
#include "legacy_gamestream.h"
#include "config.h"

namespace {

static MoonlightEventCallback s_callback = NULL;
static void *s_userdata = NULL;
static MoonlightHost s_current_host = {0};
static bool s_has_current_host = false;
static MoonlightConnectionState s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
static LegacyGameStreamServer s_server;
static bool s_game_stream_initialized = false;

static void emit(MoonlightEventType type,
                 int result,
                 int host_id,
                 int application_id,
                 const char *address)
{
    if (!s_callback) return;

    MoonlightEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.result = result;
    event.host_id = host_id;
    event.application_id = application_id;
    event.address = address;
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

static int make_key_directory(const MoonlightHost *host, char *out, size_t size)
{
    const char *name;

    if (!host || !out || size == 0) {
        return -1;
    }

    name = host->name[0] ? host->name : host->internal;

    int length = snprintf(out, size, "%s%s", config.key_dir, name);
    if (length < 0 || (size_t)length >= size) {
        return -1;
    }

    int result = sceIoMkdir(out, 0777);
    if (result < 0 && result != 0x80010011) {
        return result;
    }

    return 0;
}

static void update_host_from_server()
{
    s_current_host.paired = s_server.paired ? 1 : 0;

    if (s_server.mac[0]) {
        strncpy(s_current_host.mac, s_server.mac, sizeof(s_current_host.mac) - 1);
        s_current_host.mac[sizeof(s_current_host.mac) - 1] = '\0';
    }
}

}

int moonlight_api_init(void)
{
    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
    s_game_stream_initialized = false;
    memset(&s_server, 0, sizeof(s_server));

    return legacy_host_discovery_init(on_discovery_event, NULL);
}

void moonlight_api_shutdown(void)
{
    legacy_gamestream_shutdown(&s_server);
    s_game_stream_initialized = false;

    legacy_host_discovery_shutdown();

    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
    memset(&s_server, 0, sizeof(s_server));
    memset(&s_current_host, 0, sizeof(s_current_host));
    s_callback = NULL;
    s_userdata = NULL;
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
    emit(MOONLIGHT_EVENT_HOST_SCAN_STARTED, 0, -1, -1, NULL);

    int result = legacy_host_discovery_start();
    if (result != 0) {
        emit(MOONLIGHT_EVENT_HOST_SCAN_FAILED, result, -1, -1, NULL);
    }

    return result;
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
    char key_directory[512];

    if (!host || !host->internal[0]) {
        return -1;
    }

    if (make_key_directory(host, key_directory, sizeof(key_directory)) != 0) {
        emit(
            MOONLIGHT_EVENT_CONNECTION_FAILED,
            -1,
            host->id,
            -1,
            host->internal);
        return -1;
    }

    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }

    int result = legacy_gamestream_init(
        &s_server,
        host->internal,
        host->port,
        key_directory,
        0,
        true);

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

int moonlight_api_pair_current_host(const char pin[5])
{
    int result;

    if (!pin || !s_has_current_host ||
        s_connection_state != MOONLIGHT_CONNECTION_READY ||
        !s_game_stream_initialized) {
        return -1;
    }

    result = legacy_gamestream_pair(&s_server, pin);
    if (result != LEGACY_GAMESTREAM_OK) {
        vita_debug_log(
            "[GameStream] pairing failed for %s: %d (%s)",
            s_current_host.internal,
            result,
            legacy_gamestream_error());

        emit(
            MOONLIGHT_EVENT_PAIRING_FAILED,
            result,
            s_current_host.id,
            -1,
            s_current_host.internal);
        return result;
    }

    update_host_from_server();
    s_connection_state = MOONLIGHT_CONNECTION_PAIRED;

    int save_result = legacy_device_store_mark_paired(&s_current_host);
    if (save_result != 0) {
        vita_debug_log(
            "[GameStream] pairing succeeded but device persistence failed: %d",
            save_result);
    }

    emit(
        MOONLIGHT_EVENT_PAIRING_FINISHED,
        save_result,
        s_current_host.id,
        -1,
        s_current_host.internal);

    return 0;
}

int moonlight_api_get_applications(MoonlightApplication *out, int capacity)
{
    (void)out;
    (void)capacity;

    if (!s_has_current_host ||
        (s_connection_state != MOONLIGHT_CONNECTION_PAIRED &&
         s_connection_state != MOONLIGHT_CONNECTION_STREAMING &&
         s_connection_state != MOONLIGHT_CONNECTION_PAUSED)) {
        return -1;
    }

    return 0;
}

int moonlight_api_start_application(int application_id)
{
    if (!s_has_current_host || s_connection_state != MOONLIGHT_CONNECTION_PAIRED) {
        return -1;
    }

    s_connection_state = MOONLIGHT_CONNECTION_STREAMING;
    emit(MOONLIGHT_EVENT_STREAM_STARTED,
         0,
         s_current_host.id,
         application_id,
         s_current_host.internal);
    return 0;
}

int moonlight_api_stop_application(void)
{
    if (!s_has_current_host || s_connection_state != MOONLIGHT_CONNECTION_STREAMING) {
        return -1;
    }

    s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    emit(MOONLIGHT_EVENT_STREAM_STOPPED, 0, s_current_host.id, -1, s_current_host.internal);
    return 0;
}

int moonlight_api_disconnect_host(void)
{
    if (!s_has_current_host) {
        return 0;
    }

    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }

    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    emit(MOONLIGHT_EVENT_CONNECTION_CLOSED,
         0,
         s_current_host.id,
         -1,
         s_current_host.internal);

    s_has_current_host = false;
    return 0;
}

MoonlightConnectionState moonlight_api_get_connection_state(void)
{
    return s_connection_state;
}
