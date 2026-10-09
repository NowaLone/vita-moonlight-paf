#pragma once

#include <psp2/kernel/clib.h>

void vita_debug_set_file_logging(int enabled);
void vita_debug_log(const char *fmt, ...);
