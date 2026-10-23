#ifndef VITA_MOONLIGHT_API_H
#define VITA_MOONLIGHT_API_H

#include "moonlight/types.h"
#include "moonlight/cancellation.h"
#include "moonlight/stream_stats.h"
#include "moonlight/controller_light.h"

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
int moonlight_api_get_discovered_hosts(MoonlightHost *out, int capacity);
int moonlight_api_search_hosts(void);
int moonlight_api_stop_host_search(void);
int moonlight_api_add_host(const char *address, uint16_t port, const char *name);

/* Selected-host control calls below use non-waiting admission: -1 may mean another
 * request or disconnect owns the host. Cached app reads use the same guard.
 * Callers must not wait/retry inside an API event callback; the frontend queues
 * events for main-thread handling. Settings/discovery are independent of this gate. */
int moonlight_api_connect_host(const MoonlightHost *host);
int moonlight_api_prepare_pairing(char out_pin[5]);
int moonlight_api_pair_current_host(const char pin[5], const MoonlightCancellation *cancel);

int moonlight_api_get_applications(MoonlightApplication *out, int capacity);
int moonlight_api_start_application(int application_id, int quit_application_id);
int moonlight_api_stop_application(void);
/* Zero accepts deferred cleanup (or an idle/duplicate no-op), not final closure.
 * CONNECTION_CLOSED publishes completion; new host requests stay excluded until
 * its callback returns and the cleanup worker releases admission. */
int moonlight_api_disconnect_host(void);
MoonlightConnectionState moonlight_api_get_connection_state(void);
int moonlight_api_get_stream_stats(MoonlightStreamStats *out);
int moonlight_api_get_controller_light(MoonlightControllerLight *out);

#ifdef __cplusplus
}
#endif

#endif
