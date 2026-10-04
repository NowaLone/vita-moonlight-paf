#ifndef VITA_MOONLIGHT_MOTION_H
#define VITA_MOONLIGHT_MOTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void vita_motion_init(void);
void vita_motion_start(int enabled);
void vita_motion_stop(void);
void vita_motion_set_state(
    uint16_t controller,
    uint8_t motion_type,
    uint16_t report_rate_hz);

#ifdef __cplusplus
}
#endif

#endif
