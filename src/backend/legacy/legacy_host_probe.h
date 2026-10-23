#ifndef VITA_MOONLIGHT_LEGACY_HOST_PROBE_H
#define VITA_MOONLIGHT_LEGACY_HOST_PROBE_H
#include "moonlight/types.h"
#include "moonlight/cancellation.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 1 = TCP accepted, 0 = no confirmation, -1 = local failure, -2 = cancelled. */
int legacy_host_probe(const MoonlightHost *host, const MoonlightCancellation *cancel);
#ifdef __cplusplus
}
#endif
#endif
