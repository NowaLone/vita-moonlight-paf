#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ini.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/rng.h>
#include "wake_on_lan.h"

#include "device.h"
#include "debug.h"
#include "config.h"

#define DEVICE_FILE "device.ini"


#define INT(v) atoi((v))
#define BOOL(v) strcmp((v), "true") == 0
#define write_int(fd, key, value) fprintf(fd, "%s = %d\n", key, value)
#define write_bool(fd, key, value) fprintf(fd, "%s = %s\n", key, value ? "true" : "false");
#define write_string(fd, key, value) fprintf(fd, "%s = %s\n", key, value)

static bool generate_host_id(device_info_t *info) {
  unsigned char random_bytes[16];
  static const char hex[] = "0123456789abcdef";

  if (!info) return false;
  if (sceKernelGetRandomNumber(random_bytes, sizeof(random_bytes)) < 0) return false;

  for (int i = 0; i < (int)sizeof(random_bytes); ++i) {
    info->host_id[i * 2] = hex[random_bytes[i] >> 4];
    info->host_id[i * 2 + 1] = hex[random_bytes[i] & 0x0f];
  }
  info->host_id[32] = '\0';
  return true;
}

static void ensure_host_identity(device_info_t *info) {
  if (!info) return;

  if (info->host_id[0] == '\0') {
    (void)generate_host_id(info);
  }

  if (info->storage_name[0] == '\0') {
    const char *storage_name = info->host_id[0] ? info->host_id : info->name;
    strncpy(info->storage_name, storage_name, sizeof(info->storage_name) - 1);
    info->storage_name[sizeof(info->storage_name) - 1] = '\0';
  }
}

// Elimina la carpeta y el archivo del dispositivo
bool remove_device(const char *name) {
  int idx = -1;
  for (int i = 0; i < known_devices.count; i++) {
    if (!strcmp(known_devices.devices[i].name, name)) {
      idx = i;
      break;
    }
  }
  if (idx == -1) {
    vita_debug_log("remove_device: device %s not found\n", name);
    return false;
  }
  // Capture the persistent storage location before compacting the array.
  char storage_name[256];
  strncpy(
      storage_name,
      known_devices.devices[idx].storage_name[0]
          ? known_devices.devices[idx].storage_name
          : known_devices.devices[idx].name,
      sizeof(storage_name) - 1);
  storage_name[sizeof(storage_name) - 1] = '\0';

  // Eliminar del arreglo
  for (int i = idx; i < known_devices.count - 1; i++) {
    known_devices.devices[i] = known_devices.devices[i + 1];
  }
  known_devices.count--;

  // Eliminar del disco
  char dir_path[512];
  snprintf(dir_path, sizeof(dir_path), "%s%s", config.key_dir, storage_name);
  char file_path[512];
  device_file_path(file_path, storage_name);
  sceIoRemove(file_path); // Elimina device.ini
  // Elimina todos los archivos dentro de la carpeta antes de borrar la carpeta
  SceIoDirent dirent;
  SceUID dfd = sceIoDopen(dir_path);
  if (dfd >= 0) {
    while (sceIoDread(dfd, &dirent) > 0) {
      if (strcmp(dirent.d_name, ".") == 0 || strcmp(dirent.d_name, "..") == 0) continue;
      char full_path[512];
      snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dirent.d_name);
      sceIoRemove(full_path);
    }
    sceIoDclose(dfd);
  }
  sceIoRmdir(dir_path);
  vita_debug_log("remove_device: device %s removed from memory and disk\n", name);
  return true;
}


device_infos_t known_devices = {0};

device_info_t* find_device(const char *name) {
  // TODO: mutex
  for (int i = 0; i < known_devices.count; i++) {
    if (!strcmp(name, known_devices.devices[i].name)) {
      return &known_devices.devices[i];
    }
  }
  return NULL;
}

device_info_t* find_device_by_host_id(const char *host_id) {
  if (host_id == NULL || host_id[0] == '\0') return NULL;
  for (int i = 0; i < known_devices.count; i++) {
    if (strcmp(known_devices.devices[i].host_id, host_id) == 0) {
      return &known_devices.devices[i];
    }
  }
  return NULL;
}

device_info_t* find_device_by_address(const char *address) {
  if (address == NULL || address[0] == '\0')
    return NULL;
  for (int i = 0; i < known_devices.count; i++) {
    device_info_t *d = &known_devices.devices[i];
    if ((d->internal[0] && strcmp(d->internal, address) == 0) ||
        (d->external[0] && strcmp(d->external, address) == 0)) {
      return d;
    }
  }
  return NULL;
}

static device_info_t* find_device_by_identity(const device_info_t *info) {
  if (!info)
    return NULL;

  device_info_t *device = NULL;

  if (info->host_id[0]) {
    device = find_device_by_host_id(info->host_id);
    if (device)
      return device;
  }

  if (info->mac[0]) {
    for (int i = 0; i < known_devices.count; ++i) {
      if (known_devices.devices[i].mac[0] &&
          strcmp(known_devices.devices[i].mac, info->mac) == 0) {
        return &known_devices.devices[i];
      }
    }
  }

  if (info->internal[0]) {
    device = find_device_by_address(info->internal);
    if (device)
      return device;
  }

  if (info->external[0]) {
    device = find_device_by_address(info->external);
    if (device)
      return device;
  }

  // Name is only useful when no stronger host identity is available.
  // Different PCs may legitimately advertise the same display name.
  if (info->name[0] &&
      !info->mac[0] &&
      !info->internal[0] &&
      !info->external[0]) {
    device = find_device(info->name);
    if (device)
      return device;
  }

  return NULL;
}

void device_file_path(char *out, const char *dir) {
  snprintf(out, 512, "%s%s/%s", config.key_dir, dir, DEVICE_FILE);
}

static int device_ini_handle(void *out, const char *section, const char *name,
                             const char *value) {
  device_info_t *info = out;

  if (strcmp(name, "name") == 0) {
    strncpy(info->name, value, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
  } else if (strcmp(name, "paired") == 0) {
    info->paired = BOOL(value);
  } else if (strcmp(name, "host_id") == 0) {
    strncpy(info->host_id, value, sizeof(info->host_id) - 1);
    info->host_id[sizeof(info->host_id) - 1] = '\0';
  } else if (strcmp(name, "internal") == 0) {
    strncpy(info->internal, value, 255);
  } else if (strcmp(name, "external") == 0) {
    strncpy(info->external, value, 255);
  } else if (strcmp(name, "mac") == 0) {
    strncpy(info->mac, value, 17);
    info->mac[17] = '\0';
  } else if (strcmp(name, "port") == 0) {
    info->port = INT(value);
  } else if (strcmp(name, "prefer_external") == 0) {
    info->prefer_external = BOOL(value);
  }
  return 1;
}

device_info_t* append_device(device_info_t *info) {
  if (!info) {
    return NULL;
  }

  device_info_t *existing = find_device_by_identity(info);
  if (existing) {
    if (info->paired) {
      existing->paired = true;
    }
    if (info->name[0] && strcmp(existing->name, info->name) != 0) {
      strncpy(existing->name, info->name, sizeof(existing->name) - 1);
      existing->name[sizeof(existing->name) - 1] = '\0';
    }
    if (info->internal[0]) {
      strncpy(existing->internal, info->internal, 255);
      existing->internal[255] = '\0';
    }
    if (info->external[0]) {
      strncpy(existing->external, info->external, 255);
      existing->external[255] = '\0';
    }
    if (info->mac[0]) {
      strncpy(existing->mac, info->mac, 17);
      existing->mac[17] = '\0';
    }
    if (existing->host_id[0] == '\0' && info->host_id[0]) {
      strncpy(existing->host_id, info->host_id, sizeof(existing->host_id) - 1);
      existing->host_id[sizeof(existing->host_id) - 1] = '\0';
    }
    if (existing->storage_name[0] == '\0' && info->storage_name[0]) {
      strncpy(existing->storage_name, info->storage_name, sizeof(existing->storage_name) - 1);
      existing->storage_name[sizeof(existing->storage_name) - 1] = '\0';
    }
    if (info->port != 0) {
      existing->port = info->port;
    }
    existing->prefer_external = info->prefer_external;
    vita_debug_log("append_device: device %s already exists, merging\n", existing->name);
    return existing;
  }

  // FIXME: need mutex
  if (known_devices.size == 0) {
    vita_debug_log("append_device: allocating memory for the initial device list...\n");
    known_devices.devices = malloc(sizeof(device_info_t) * 4);
    if (known_devices.devices == NULL) {
      vita_debug_log("append_device: failed to allocate memory for the initial device list\n");
      return NULL;
    }
    known_devices.size = 4;
  } else if (known_devices.size == known_devices.count) {
    vita_debug_log("append_device: the device list is full, resizing...\n");
    //if (known_devices.size == 64) {
    //  return false;
    //}
    size_t new_size = sizeof(device_info_t) * (known_devices.size * 2);
    device_info_t *tmp = realloc(known_devices.devices, new_size);
    if (tmp == NULL) {
      vita_debug_log("append_device: failed to resize the device list\n");
      return NULL;
    }
    known_devices.devices = tmp;
    known_devices.size *= 2;
  }
  device_info_t *p = &known_devices.devices[known_devices.count];

  ensure_host_identity(info);

  strncpy(p->name, info->name, 255);
  p->paired = info->paired;
  strncpy(p->internal, info->internal, 255);
  strncpy(p->external, info->external, 255);
  strncpy(p->mac, info->mac, 17);
  p->mac[17] = '\0';
  strncpy(p->host_id, info->host_id, sizeof(p->host_id) - 1);
  p->host_id[sizeof(p->host_id) - 1] = '\0';
  strncpy(p->storage_name, info->storage_name, sizeof(p->storage_name) - 1);
  p->storage_name[sizeof(p->storage_name) - 1] = '\0';
  p->port = info->port;
  p->prefer_external = info->prefer_external;
  vita_debug_log("append_device: device %s is added to the list\n", p->name);

  known_devices.count++;
  return p;
}

bool update_device(device_info_t *info) {
  device_info_t *p = find_device(info->name);
  if (p == NULL) {
    return false;
  }

  //strncpy(p->name, info->name, 255);
  p->paired = info->paired;
  strncpy(p->internal, info->internal, 255);
  strncpy(p->external, info->external, 255);
  p->port = info->port;
  p->prefer_external = info->prefer_external;
  return true;
}

void load_all_known_devices() {
  //struct stat st;
  device_info_t info;

  SceUID dfd = sceIoDopen(config.key_dir);
  if (dfd < 0) {
    return;
  }
  do {
    SceIoDirent ent = {0};
    if (sceIoDread(dfd, &ent) <= 0) {
      break;
    }
    if (strcmp(".", ent.d_name) == 0 || strcmp("..", ent.d_name) == 0) {
      continue;
    }
    if (!SCE_S_ISDIR(ent.d_stat.st_mode)) {
      continue;
    }

    memset(&info, 0, sizeof(device_info_t));
    strncpy(info.name, ent.d_name, 255);
    info.name[255] = '\0';
    strncpy(info.storage_name, ent.d_name, sizeof(info.storage_name) - 1);
    info.storage_name[sizeof(info.storage_name) - 1] = '\0';
    if (!load_device_info(&info)) {
      continue;
    }
    if (!info.host_id[0]) {
      ensure_host_identity(&info);
      save_device_info(&info);
    }
    append_device(&info);
  } while(true);

  sceIoDclose(dfd);
  return;
}

bool load_device_info(device_info_t *info) {
  char path[512] = {0};
  device_file_path(path, info->name);
  vita_debug_log("load_device_info: reading %s\n", path);

  // for backward compatibility
  info->port = 47989;
  int ret = ini_parse(path, device_ini_handle, info);
  if (!ret) {
    vita_debug_log("load_device_info: device found:\n", ret);
    vita_debug_log("load_device_info:   info->name = %s\n", info->name);
    vita_debug_log("load_device_info:   info->paired = %s\n", info->paired ? "true" : "false");
    vita_debug_log("load_device_info:   info->internal = %s\n", info->internal);
    vita_debug_log("load_device_info:   info->external = %s\n", info->external);
    vita_debug_log("load_device_info:   info->port= %d\n", info->port);
    vita_debug_log("load_device_info:   info->prefer_external = %s\n", info->prefer_external ? "true" : "false");
    return true;
  } else {
    vita_debug_log("load_device_info: ini_parse returned %d\n", ret);
    return false;
  }
}

void save_device_info(const device_info_t *info) {
  char path[512] = {0};
  const char *storage_name;
  if (!info) return;
  storage_name = info->storage_name[0] ? info->storage_name : info->name;
  device_file_path(path, storage_name);
  vita_debug_log("save_device_info: device file path: %s\n", path);

  // Ya no se intenta obtener la MAC por ARP. Solo se guarda la que esté en info->mac.

  FILE* fd = fopen(path, "w");
  if (!fd) {
    // FIXME
    vita_debug_log("save_device_info: cannot open device file\n");
    return;
  }

  vita_debug_log("save_device_info: name = %s\n", info->name);
  write_string(fd, "name", info->name);

  vita_debug_log("save_device_info: paired = %s\n", info->paired ? "true" : "false");
  write_bool(fd, "paired", info->paired);
  write_string(fd, "host_id", info->host_id);

  vita_debug_log("save_device_info: internal = %s\n", info->internal);
  write_string(fd, "internal", info->internal);

  vita_debug_log("save_device_info: external = %s\n", info->external);
  write_string(fd, "external", info->external);

  vita_debug_log("save_device_info: mac = %s\n", info->mac);
  write_string(fd, "mac", info->mac);

  vita_debug_log("save_device_info: port = %d\n", info->port);
  write_int(fd, "port", info->port);

  vita_debug_log("save_device_info: prefer_external = %s\n", info->prefer_external ? "true" : "false");
  write_bool(fd, "prefer_external", info->prefer_external);

  fclose(fd);
  vita_debug_log("save_device_info: file closed\n");
}
