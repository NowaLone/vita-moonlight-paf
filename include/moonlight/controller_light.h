#ifndef VITA_MOONLIGHT_CONTROLLER_LIGHT_H
#define VITA_MOONLIGHT_CONTROLLER_LIGHT_H

#include <stdint.h>

typedef enum MoonlightControllerLightMode {
    MOONLIGHT_CONTROLLER_LIGHT_OFF = 0,
    MOONLIGHT_CONTROLLER_LIGHT_DS4 = 1,
    MOONLIGHT_CONTROLLER_LIGHT_DUALSENSE = 2
} MoonlightControllerLightMode;

/* Cached host feedback, independent of video frames and the statistics toggle.
 * OFF also means the session is stopping/finished. RGB starts black each session. */
typedef struct MoonlightControllerLight {
    uint32_t generation;
    int mode;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} MoonlightControllerLight;

#endif
