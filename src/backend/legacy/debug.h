#pragma once

#include <psp2/kernel/clib.h>

#ifdef __cplusplus
extern "C" {
#endif

void vita_debug_log(const char *fmt, ...);
void vita_debug_log_set_enabled(int enabled);

#ifdef __cplusplus
}
#endif
