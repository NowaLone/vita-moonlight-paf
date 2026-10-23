#ifndef VITA_MOONLIGHT_LEGACY_HOST_DISCOVERY_H
#define VITA_MOONLIGHT_LEGACY_HOST_DISCOVERY_H

#include "moonlight/types.h"

#ifdef __cplusplus
extern "C" {
#endif

int legacy_host_discovery_init(void);
/* Final teardown after the PAF run loop; joins the sole worker before NetTerm. */
void legacy_host_discovery_shutdown(void);
/* Main-thread control: renew/start a generation, or request cancellation; never join. */
int legacy_host_discovery_start(uint32_t *generation);
int legacy_host_discovery_stop(void);
int legacy_host_discovery_is_idle(void);
/* Connect worker only: wait for the old generation to release its socket. */
void legacy_host_discovery_wait_idle(void);
/* 1 changed, 0 unchanged, -1 inactive; copies a coherent publication under lock. */
int legacy_host_discovery_read(MoonlightDiscoverySnapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
