#ifndef VITA_MOONLIGHT_SETTINGS_SERVICE_H
#define VITA_MOONLIGHT_SETTINGS_SERVICE_H

#include "backend/moonlight_backend.h"

class SettingsService {
public:
    explicit SettingsService(MoonlightBackend &backend);

    int Open();
    int GetAll(MoonlightSettings *out);
    int GetValue(MoonlightSettingKey key, int *out_value);
    int SetValue(MoonlightSettingKey key, int value);

private:
    MoonlightBackend &m_backend;
};

#endif
