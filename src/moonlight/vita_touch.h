#ifndef VITA_MOONLIGHT_TOUCH_H
#define VITA_MOONLIGHT_TOUCH_H

#include <psp2/touch.h>

#ifdef __cplusplus
extern "C" {
#endif

void vita_touch_init(void);
void vita_touch_configure(int mode, int mouse_acceleration);
void vita_touch_start(void);
void vita_touch_stop(void);
void vita_touch_process(void);

#ifdef __cplusplus
}
#endif

#endif
