#pragma once

#include "moonlight/types.h"

int legacy_device_store_init(void);
int legacy_device_store_get_hosts(MoonlightHost *out, int capacity);
int legacy_device_store_add_host(const char *address, uint16_t port, const char *name);
