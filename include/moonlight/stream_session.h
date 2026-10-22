#pragma once

#include "moonlight/types.h"
#include "moonlight/stream_stats.h"
#include "moonlight/controller_light.h"
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
int moonlight_stream_is_running(void);
int moonlight_stream_get_stats(MoonlightStreamStats *out);
int moonlight_stream_get_controller_light(MoonlightControllerLight *out);

#ifdef __cplusplus
}
#endif
