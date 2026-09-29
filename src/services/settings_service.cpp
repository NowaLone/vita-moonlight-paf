#include "services/settings_service.h"

SettingsService::SettingsService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int SettingsService::Open()
{
    return m_backend.OpenSettings();
}

int SettingsService::GetAll(MoonlightSettings *out)
{
    return m_backend.GetSettings(out);
}

int SettingsService::GetValue(MoonlightSettingKey key, int *out_value)
{
    return m_backend.GetSettingValue(key, out_value);
}

int SettingsService::SetValue(MoonlightSettingKey key, int value)
{
    return m_backend.SetSettingValue(key, value);
}
