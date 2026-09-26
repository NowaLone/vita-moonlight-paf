#ifndef VITA_MOONLIGHT_CONFIG_H
#define VITA_MOONLIGHT_CONFIG_H
#include "moonlight/api.h"
#ifdef __cplusplus
extern "C" {
#endif
int moonlight_config_init(void); void moonlight_config_shutdown(void); const char* moonlight_config_path(void);
int moonlight_config_get(MoonlightSettings*); int moonlight_config_get_value(MoonlightSettingKey,int*);
int moonlight_config_set_value(MoonlightSettingKey,int); int moonlight_config_set_motion_scalar_x(float); int moonlight_config_set_motion_scalar_y(float);
int moonlight_config_save(void);
#ifdef __cplusplus
}
#endif
#endif
