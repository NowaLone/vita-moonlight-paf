#include "backend/legacy_moonlight_adapter.h"

#include "moonlight/api.h"
#include "moonlight/settings.h"

extern "C" int moonlight_api_stop_host_search(void);

LegacyMoonlightAdapter::LegacyMoonlightAdapter()
{
}

LegacyMoonlightAdapter::~LegacyMoonlightAdapter()
{
}

int LegacyMoonlightAdapter::Initialize()
{
    return moonlight_api_init();
}

void LegacyMoonlightAdapter::Shutdown()
{
    moonlight_settings_shutdown();
    moonlight_api_shutdown();
}

int LegacyMoonlightAdapter::OpenSettings()
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_open();
}

int LegacyMoonlightAdapter::GetSettings(MoonlightSettings *out)
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_get_all(out);
}

int LegacyMoonlightAdapter::GetSettingValue(MoonlightSettingKey key, int *out_value)
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_get_value(key, out_value);
}

int LegacyMoonlightAdapter::SetSettingValue(MoonlightSettingKey key, int value)
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_set_value(key, value);
}

int LegacyMoonlightAdapter::SetEventCallback(MoonlightEventCallback callback, void *userdata)
{
    int result = moonlight_api_set_event_callback(callback, userdata);
    if (result != 0) {
        return result;
    }

    return moonlight_settings_set_event_callback(callback, userdata);
}

int LegacyMoonlightAdapter::GetHosts(MoonlightHost *out, int capacity)
{
    return moonlight_api_get_hosts(out, capacity);
}

int LegacyMoonlightAdapter::SearchHosts()
{
    return moonlight_api_search_hosts();
}

int LegacyMoonlightAdapter::StopHostSearch()
{
    return moonlight_api_stop_host_search();
}

int LegacyMoonlightAdapter::AddHost(const char *address, uint16_t port, const char *name)
{
    return moonlight_api_add_host(address, port, name);
}

int LegacyMoonlightAdapter::ConnectHost(const MoonlightHost &host)
{
    return moonlight_api_connect_host(&host);
}

int LegacyMoonlightAdapter::PairCurrentHost()
{
    return moonlight_api_pair_current_host();
}

int LegacyMoonlightAdapter::GetApplications(MoonlightApplication *out, int capacity)
{
    return moonlight_api_get_applications(out, capacity);
}

int LegacyMoonlightAdapter::StartApplication(int application_id)
{
    return moonlight_api_start_application(application_id);
}

int LegacyMoonlightAdapter::StopApplication()
{
    return moonlight_api_stop_application();
}

int LegacyMoonlightAdapter::DisconnectHost()
{
    return moonlight_api_disconnect_host();
}

MoonlightConnectionState LegacyMoonlightAdapter::GetConnectionState() const
{
    return moonlight_api_get_connection_state();
}
