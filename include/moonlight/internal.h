#ifndef VITA_MOONLIGHT_INTERNAL_H
#define VITA_MOONLIGHT_INTERNAL_H

#include "moonlight/api.h"

/* Transitional implementation-only entry points used by LegacyMoonlightAdapter. */
#ifdef __cplusplus
extern "C" {
#endif

int moonlight_api_stop_host_search(void);

#ifdef __cplusplus
}
#endif

#endif /* VITA_MOONLIGHT_INTERNAL_H */
