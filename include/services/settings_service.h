#ifndef VITA_MOONLIGHT_SETTINGS_SERVICE_H
#define VITA_MOONLIGHT_SETTINGS_SERVICE_H

#include "moonlight/api.h"

class SettingsService {
public:
    int Open();
    int GetAll(MoonlightSettings *out);
    int GetValue(MoonlightSettingKey key, int *out_value);
    int SetValue(MoonlightSettingKey key, int value);
};

#endif
