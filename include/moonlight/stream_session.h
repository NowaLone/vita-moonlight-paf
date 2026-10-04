#pragma once

#include "moonlight/types.h"
#include "legacy_gamestream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*MoonlightStreamEventCallback)(
    MoonlightEventType type,
    int result,
    void *userdata);

int moonlight_stream_start(
    LegacyGameStreamServer *server,
    const char *unique_path,
    int application_id,
    const MoonlightSettings *settings,
    MoonlightStreamEventCallback callback,
    void *userdata);

int moonlight_stream_stop(void);

#ifdef __cplusplus
}
#endif
