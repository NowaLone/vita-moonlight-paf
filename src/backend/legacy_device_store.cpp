#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/stat.h>

#include "backend/legacy_device_store.h"
#include "device.h"
#include "config.h"

namespace {

static bool s_initialized = false;

static bool ensure_directory(const char *path)
{
    int result = sceIoMkdir(path, 0777);
    return result >= 0 || result == 0x80010011;
}

static bool ensure_device_directory(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", config.key_dir, name);
    return ensure_directory(path);
}

static void copy_string(char *destination, size_t size, const char *source)
{
    if (!destination || size == 0) {
        return;
    }

    destination[0] = '\0';
    if (!source) {
        return;
    }

    strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
}

static void fill_host(const device_info_t *device, int id, MoonlightHost *host)
{
    *host = MoonlightHost();
    host->id = id;
    host->port = device->port;
    host->paired = device->paired ? 1 : 0;
    host->online = 0;
    host->prefer_external = device->prefer_external ? 1 : 0;

    copy_string(host->name, sizeof(host->name), device->name);
    copy_string(host->internal, sizeof(host->internal), device->internal);
    copy_string(host->external, sizeof(host->external), device->external);
    copy_string(host->mac, sizeof(host->mac), device->mac);
}

}

int legacy_device_store_init(void)
{
    if (s_initialized) {
        return 0;
    }

#ifdef USE_DIR_UMA0
    static const char *paths[] = {
        "uma0:data/moonlight/"
    };
#else
    static const char *paths[] = {
        "ux0:data/moonlight/",
        "ux0:moonlight/",
        "uma0:data/moonlight/"
    };
#endif

    const size_t path_count = sizeof(paths) / sizeof(paths[0]);

    for (size_t i = 0; i < path_count; ++i) {
        if (!ensure_directory(paths[i])) {
            continue;
        }

        copy_string(config.key_dir, sizeof(config.key_dir), paths[i]);
        load_all_known_devices();
        s_initialized = true;
        return 0;
    }

    return -1;
}

int legacy_device_store_get_hosts(MoonlightHost *out, int capacity)
{
    if (!s_initialized || capacity < 0) {
        return -1;
    }

    if (capacity == 0) {
        return known_devices.count;
    }

    if (!out) {
        return -1;
    }

    int count = known_devices.count;
    if (count > capacity) {
        count = capacity;
    }

    for (int i = 0; i < count; ++i) {
        fill_host(&known_devices.devices[i], i, &out[i]);
    }

    return count;
}

int legacy_device_store_add_host(const char *address, uint16_t port, const char *name)
{
    if (!s_initialized || !address || !address[0]) {
        return -1;
    }

    device_info_t info;
    memset(&info, 0, sizeof(info));

    copy_string(info.name, sizeof(info.name), name && name[0] ? name : address);
    copy_string(info.internal, sizeof(info.internal), address);
    info.port = port != 0 ? port : 47989;

    if (!ensure_device_directory(info.name)) {
        return -1;
    }

    device_info_t *stored = append_device(&info);
    if (!stored) {
        stored = find_device(info.name);
        if (!stored) {
            return -1;
        }

        copy_string(stored->internal, sizeof(stored->internal), info.internal);
        stored->port = info.port;
    }

    save_device_info(stored);
    return 0;
}

int legacy_device_store_mark_paired(const MoonlightHost *host)
{
    if (!s_initialized || !host || !host->internal[0]) {
        return -1;
    }

    device_info_t *stored = NULL;
    bool created = false;
    if (host->name[0]) {
        stored = find_device(host->name);
    }
    if (!stored) {
        stored = find_device_by_address(host->internal);
    }

    if (!stored) {
        if (legacy_device_store_add_host(host->internal, host->port, host->name) != 0) {
            return -1;
        }
        created = true;
        if (host->name[0]) {
            stored = find_device(host->name);
        }
        if (!stored) {
            stored = find_device_by_address(host->internal);
        }
    }

    if (!stored) {
        return -1;
    }

    if (created && host->name[0]) {
        copy_string(stored->name, sizeof(stored->name), host->name);
    }
    copy_string(stored->internal, sizeof(stored->internal), host->internal);
    copy_string(stored->external, sizeof(stored->external), host->external);
    copy_string(stored->mac, sizeof(stored->mac), host->mac);
    stored->port = host->port != 0 ? host->port : stored->port;
    stored->prefer_external = host->prefer_external != 0;
    stored->paired = true;
    save_device_info(stored);
    return 0;
}
