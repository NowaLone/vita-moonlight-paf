#ifndef VITA_MOONLIGHT_VITA_INPUT_H
#define VITA_MOONLIGHT_VITA_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vita_input_configure(
    int controller_type,
    int swap_shoulder_buttons,
    int ps_button_capture,
    int touchscreen_mode,
    int mouse_acceleration,
    int motion_enabled,
    int back_deadzone_top,
    int back_deadzone_right,
    int back_deadzone_bottom,
    int back_deadzone_left);

void vita_input_set_motion_state(
    uint16_t controller,
    uint8_t motion_type,
    uint16_t report_rate_hz);

void vita_input_start(void);
void vita_input_stop(void);

#ifdef __cplusplus
}
#endif

#endif
