#ifndef VITA_MOONLIGHT_API_H
#define VITA_MOONLIGHT_API_H

#include "moonlight/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Temporary C compatibility API.
 *
 * New frontend code should depend on MoonlightBackend instead. These entry
 * points model the legacy session that will be moved behind
 * LegacyMoonlightAdapter.
 */

int moonlight_api_init(void);
void moonlight_api_shutdown(void);

int moonlight_api_open_settings(void);
int moonlight_api_get_settings(MoonlightSettings *out);
int moonlight_api_get_setting_value(MoonlightSettingKey key, int *out_value);
int moonlight_api_set_setting_value(MoonlightSettingKey key, int value);
int moonlight_api_set_event_callback(MoonlightEventCallback callback, void *userdata);

int moonlight_api_get_hosts(MoonlightHost *out, int capacity);
int moonlight_api_search_hosts(void);
int moonlight_api_stop_host_search(void);
int moonlight_api_add_host(const char *address, uint16_t port, const char *name);

int moonlight_api_connect_host(const MoonlightHost *host);
int moonlight_api_pair_current_host(void);

int moonlight_api_get_applications(MoonlightApplication *out, int capacity);
int moonlight_api_start_application(int application_id);
int moonlight_api_stop_application(void);
int moonlight_api_disconnect_host(void);
MoonlightConnectionState moonlight_api_get_connection_state(void);

#ifdef __cplusplus
}
#endif

#endif
