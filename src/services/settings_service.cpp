#include "services/settings_service.h"

SettingsService::SettingsService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int SettingsService::Open()
{
    return m_backend.OpenSettings();
}
