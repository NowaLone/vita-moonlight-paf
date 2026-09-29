#include "services/settings_service.h"

int SettingsService::Open()
{
    return moonlight_api_open_settings();
}

int SettingsService::GetAll(MoonlightSettings *out)
{
    return moonlight_api_get_settings(out);
}

int SettingsService::GetValue(MoonlightSettingKey key, int *out_value)
{
    return moonlight_api_get_setting_value(key, out_value);
}

int SettingsService::SetValue(MoonlightSettingKey key, int value)
{
    return moonlight_api_set_setting_value(key, value);
}
