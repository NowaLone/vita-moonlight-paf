#pragma once

#include <stddef.h>
#include "moonlight/types.h"

int legacy_device_store_init(void);
int legacy_device_store_get_hosts(MoonlightHost *out, int capacity);
int legacy_device_store_add_host(const char *address, uint16_t port, const char *name);
int legacy_device_store_mark_paired(const MoonlightHost *host);
int legacy_device_store_delete_host(const MoonlightHost *host);
int legacy_device_store_get_client_directory(
    char *out,
    size_t size);
int legacy_device_store_get_client_unique_id_path(
    char *out,
    size_t size);

int legacy_device_store_set_host_id(
    const MoonlightHost *host,
    const char *host_id);
