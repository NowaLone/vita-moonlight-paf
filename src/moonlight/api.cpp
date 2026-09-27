#include "moonlight/api.h"
#include "moonlight/settings.h"

namespace {
static MoonlightEventCallback s_callback = NULL;
static void *s_userdata = NULL;

static void emit(MoonlightEventType type, int result, int host_id, const char *address)
{
    if (!s_callback) return;

    MoonlightEvent event;
    event.type = type;
    event.result = result;
    event.host_id = host_id;
    event.address = address;
    s_callback(&event, s_userdata);
}
}

int moonlight_api_init(void)
{
    return moonlight_settings_init();
}

void moonlight_api_shutdown(void)
{
    moonlight_settings_shutdown();
    s_callback = NULL;
    s_userdata = NULL;
}

int moonlight_api_open_settings(void)
{
    return moonlight_settings_open();
}

int moonlight_api_get_settings(MoonlightSettings *out)
{
    return moonlight_settings_get_all(out);
}

int moonlight_api_get_setting_value(MoonlightSettingKey key, int *out_value)
{
    return moonlight_settings_get_value(key, out_value);
}

int moonlight_api_set_setting_value(MoonlightSettingKey key, int value)
{
    return moonlight_settings_set_value(key, value);
}

int moonlight_api_set_event_callback(MoonlightEventCallback callback, void *userdata)
{
    s_callback = callback;
    s_userdata = userdata;
    return 0;
}

void moonlight_settings_emit_closed(const MoonlightEvent *event)
{
    if (!s_callback || !event) return;
    s_callback(event, s_userdata);
}

int moonlight_api_get_hosts(MoonlightHost *out, int capacity)
{
    (void)out;
    (void)capacity;
    return 0;
}

int moonlight_api_search_hosts(void)
{
    emit(MOONLIGHT_EVENT_HOST_SCAN_STARTED, 0, -1, NULL);
    emit(MOONLIGHT_EVENT_HOST_SCAN_FINISHED, 0, -1, NULL);
    return 0;
}

int moonlight_api_add_host(const char *address, uint16_t port, const char *name)
{
    (void)address;
    (void)port;
    (void)name;
    return 0;
}

int moonlight_api_pair_host(const char *address)
{
    (void)address;
    return 0;
}

int moonlight_api_start_stream(const char *address)
{
    (void)address;
    return 0;
}

int moonlight_api_stop_stream(void)
{
    return 0;
}

void moonlight_settings_emit_changed(const MoonlightEvent *event)
{
    if (!s_callback || !event) return;
    s_callback(event, s_userdata);
}
