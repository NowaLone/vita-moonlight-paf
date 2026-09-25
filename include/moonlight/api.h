#ifndef VITA_MOONLIGHT_API_H
#define VITA_MOONLIGHT_API_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin C boundary for xyzz/vita-moonlight.
 * PAF pages call these; libgamestream stays out of the plugin.
 */

int moonlight_api_search_hosts(void);
int moonlight_api_add_host(const char *address);
int moonlight_api_pair_host(const char *address);
int moonlight_api_start_stream(const char *address);
int moonlight_api_apply_settings(void);

#ifdef __cplusplus
}
#endif

#endif
