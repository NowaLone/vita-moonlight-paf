#ifndef VITA_MOONLIGHT_VITA_INPUT_H
#define VITA_MOONLIGHT_VITA_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

int vita_input_configure(int controller_type, int swap_shoulder_buttons, int ps_button_capture);
void vita_input_start(void);
void vita_input_stop(void);

#ifdef __cplusplus
}
#endif

#endif
