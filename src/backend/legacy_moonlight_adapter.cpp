#include "backend/legacy_moonlight_adapter.h"

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
    moonlight_api_shutdown();
}

int LegacyMoonlightAdapter::OpenSettings()
{
    return moonlight_api_open_settings();
}

int LegacyMoonlightAdapter::GetSettings(MoonlightSettings *out)
{
    return moonlight_api_get_settings(out);
}

int LegacyMoonlightAdapter::GetSettingValue(MoonlightSettingKey key, int *out_value)
{
    return moonlight_api_get_setting_value(key, out_value);
}

int LegacyMoonlightAdapter::SetSettingValue(MoonlightSettingKey key, int value)
{
    return moonlight_api_set_setting_value(key, value);
}

int LegacyMoonlightAdapter::SetEventCallback(MoonlightEventCallback callback, void *userdata)
{
    return moonlight_api_set_event_callback(callback, userdata);
}

int LegacyMoonlightAdapter::GetHosts(MoonlightHost *out, int capacity)
{
    return moonlight_api_get_hosts(out, capacity);
}

int LegacyMoonlightAdapter::SearchHosts()
{
    return moonlight_api_search_hosts();
}

int LegacyMoonlightAdapter::AddHost(const char *address, uint16_t port, const char *name)
{
    return moonlight_api_add_host(address, port, name);
}

int LegacyMoonlightAdapter::PairHost(const char *address)
{
    return moonlight_api_pair_host(address);
}

int LegacyMoonlightAdapter::StartStream(const char *address)
{
    return moonlight_api_start_stream(address);
}

int LegacyMoonlightAdapter::StopStream()
{
    return moonlight_api_stop_stream();
}
