#include <stddef.h>
#include "moonlight/api.h"
#include "moonlight/settings.h"
#include "legacy_host_discovery.h"

namespace {
static MoonlightEventCallback s_callback = NULL;
static void *s_userdata = NULL;
static MoonlightHost s_current_host = {0};
static bool s_has_current_host = false;
static MoonlightConnectionState s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;

static void emit(MoonlightEventType type,
                 int result,
                 int host_id,
                 int application_id,
                 const char *address)
{
    if (!s_callback) return;

    MoonlightEvent event;
    event.type = type;
    event.result = result;
    event.host_id = host_id;
    event.application_id = application_id;
    event.address = address;
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
}

int moonlight_api_init(void)
{
    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;

    return legacy_host_discovery_init(on_discovery_event, NULL);
}

void moonlight_api_shutdown(void)
{
    legacy_host_discovery_shutdown();

    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
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
    (void)out;
    (void)capacity;
    return 0;
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
    (void)address;
    (void)port;
    (void)name;
    return 0;
}

int moonlight_api_connect_host(const MoonlightHost *host)
{
    if (!host) {
        return -1;
    }

    s_current_host = *host;
    s_has_current_host = true;
    s_connection_state = MOONLIGHT_CONNECTION_READY;

    emit(MOONLIGHT_EVENT_CONNECTION_READY, 0, host->id, -1, host->internal);
    return 0;
}

int moonlight_api_pair_current_host(void)
{
    if (!s_has_current_host || s_connection_state != MOONLIGHT_CONNECTION_READY) {
        return -1;
    }

    s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    emit(MOONLIGHT_EVENT_PAIRING_FINISHED, 0, s_current_host.id, -1, s_current_host.internal);
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
