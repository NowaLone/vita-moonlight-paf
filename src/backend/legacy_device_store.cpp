#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
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


static int write_device_info(const device_info_t *info)
{
    if (!info || !info->name[0]) {
        return -1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s%s/device.ini", config.key_dir, info->name);

    char data[2048];
    int length = snprintf(
        data, sizeof(data),
        "paired = %s\n"
        "internal = %s\n"
        "external = %s\n"
        "mac = %s\n"
        "port = %u\n"
        "prefer_external = %s\n",
        info->paired ? "true" : "false",
        info->internal,
        info->external,
        info->mac,
        (unsigned)info->port,
        info->prefer_external ? "true" : "false");

    if (length < 0 || length >= (int)sizeof(data)) {
        return -2;
    }

    SceUID fd = sceIoOpen(
        path,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);

    if (fd < 0) {
        return (int)fd;
    }

    int written = sceIoWrite(fd, data, length);
    int close_result = sceIoClose(fd);

    if (written != length) {
        return written < 0 ? written : -3;
    }

    if (close_result < 0) {
        return close_result;
    }

    SceIoStat stat;
    if (sceIoGetstat(path, &stat) < 0) {
        return -4;
    }

    return 0;
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

    // Persist first. The old implementation created the directory before
    // append_device(), so an allocation failure could leave an empty folder
    // with no device.ini at all.
    int write_result = write_device_info(&info);
    if (write_result != 0) {
        return write_result;
    }

    device_info_t *stored = find_device(info.name);
    if (!stored) {
        append_device(&info);
        stored = find_device(info.name);
    }

    if (stored) {
        copy_string(stored->internal, sizeof(stored->internal), info.internal);
        stored->port = info.port;
    }

    return 0;
}

int legacy_device_store_mark_paired(const MoonlightHost *host)
{
    if (!s_initialized || !host || !host->internal[0]) {
        return -1;
    }

    device_info_t info;
    memset(&info, 0, sizeof(info));
    copy_string(info.name, sizeof(info.name), host->name[0] ? host->name : host->internal);
    copy_string(info.internal, sizeof(info.internal), host->internal);
    copy_string(info.external, sizeof(info.external), host->external);
    copy_string(info.mac, sizeof(info.mac), host->mac);
    info.port = host->port != 0 ? host->port : 47989;
    info.prefer_external = host->prefer_external != 0;
    info.paired = true;

    if (!ensure_device_directory(info.name)) {
        return -1;
    }

    int write_result = write_device_info(&info);
    if (write_result != 0) {
        return write_result;
    }

    device_info_t *stored = find_device(info.name);
    if (!stored) {
        stored = find_device_by_address(info.internal);
    }
    if (!stored) {
        append_device(&info);
        stored = find_device(info.name);
    }

    if (stored) {
        *stored = info;
    }

    return 0;
}
