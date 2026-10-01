#pragma once

#include <psp2/kernel/clib.h>

#define vita_debug_log(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); } while (0)
