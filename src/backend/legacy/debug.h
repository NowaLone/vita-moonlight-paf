#pragma once

#include <psp2/kernel/clib.h>

#ifdef __cplusplus
extern "C" {
#endif

void vita_debug_set_file_logging(int enabled);
void vita_debug_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
