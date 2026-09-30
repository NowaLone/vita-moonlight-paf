#ifndef VITA_MOONLIGHT_SETTINGS_H
#define VITA_MOONLIGHT_SETTINGS_H

#include "moonlight/types.h"

int moonlight_settings_init(void);
void moonlight_settings_shutdown(void);
int moonlight_settings_open(void);
int moonlight_settings_get_value(MoonlightSettingKey key, int *out_value);
int moonlight_settings_set_value(MoonlightSettingKey key, int value);
int moonlight_settings_get_all(MoonlightSettings *out);
int moonlight_settings_set_event_callback(MoonlightEventCallback callback, void *userdata);

#endif
