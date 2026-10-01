#ifndef VITA_MOONLIGHT_LEGACY_HOST_DISCOVERY_H
#define VITA_MOONLIGHT_LEGACY_HOST_DISCOVERY_H

#include "moonlight/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LegacyHostDiscoveryEventType {
    LEGACY_HOST_DISCOVERY_FOUND = 0,
    LEGACY_HOST_DISCOVERY_FINISHED
} LegacyHostDiscoveryEventType;

typedef void (*LegacyHostDiscoveryCallback)(
    LegacyHostDiscoveryEventType type,
    const MoonlightHost *host,
    void *userdata
);

int legacy_host_discovery_init(LegacyHostDiscoveryCallback callback, void *userdata);
void legacy_host_discovery_shutdown(void);

int legacy_host_discovery_start(void);
int legacy_host_discovery_stop(void);
int legacy_host_discovery_is_running(void);

int legacy_host_discovery_get_hosts(MoonlightHost *out, int capacity);

#ifdef __cplusplus
}
#endif

#endif
