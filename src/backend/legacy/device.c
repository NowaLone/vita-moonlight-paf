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

static void ensure_host_identity(device_info_t *info) {
  if (!info) return;

  /*
   * host_id is the GameStream server UUID and is assigned only after
   * /serverinfo has been queried. It must never be generated on the client.
   */
  if (info->storage_name[0] == '\0') {
    const char *storage_name = info->name[0] ? info->name : info->host_id;
    strncpy(info->storage_name, storage_name, sizeof(info->storage_name) - 1);
    info->storage_name[sizeof(info->storage_name) - 1] = '\0';
  }
}

// Elimina la carpeta y el archivo del dispositivo
static bool device_has_credentials(const device_info_t *info) {
  char path[512];
  FILE *file;

  if (!info || !info->storage_name[0]) {
    return false;
  }

  snprintf(path, sizeof(path), "%s%s/client.pem",
      config.key_dir, info->storage_name);
  file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  fclose(file);

  snprintf(path, sizeof(path), "%s%s/key.pem",
      config.key_dir, info->storage_name);
  file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  fclose(file);

  return true;
}

static bool devices_same_identity(
    const device_info_t *left,
    const device_info_t *right) {
  if (!left || !right) {
    return false;
  }

  if (left->host_id[0] && right->host_id[0] &&
      strcmp(left->host_id, right->host_id) == 0) {
    return true;
  }

  if (left->mac[0] && right->mac[0] &&
      strcmp(left->mac, right->mac) == 0) {
    return true;
  }

  if (left->internal[0] && right->internal[0] &&
      strcmp(left->internal, right->internal) == 0) {
    return true;
  }

  if (left->external[0] && right->external[0] &&
      strcmp(left->external, right->external) == 0) {
    return true;
  }

  if (!left->internal[0] && !left->external[0] &&
      !right->internal[0] && !right->external[0] &&
      left->name[0] && right->name[0] &&
      strcmp(left->name, right->name) == 0) {
    return true;
  }

  return false;
}

static bool should_keep_device(
    const device_info_t *candidate,
    const device_info_t *current) {
  bool candidate_credentials;
  bool current_credentials;

  if (!candidate || !current) {
    return false;
  }

  if (candidate->paired != current->paired) {
    return candidate->paired;
  }

  candidate_credentials = device_has_credentials(candidate);
  current_credentials = device_has_credentials(current);

  if (candidate_credentials != current_credentials) {
    return candidate_credentials;
  }

  if (candidate->host_id[0] != current->host_id[0]) {
    return candidate->host_id[0] != '\0';
  }

  return strcmp(candidate->storage_name, current->storage_name) < 0;
}

static void merge_device_info(
    device_info_t *destination,
    const device_info_t *source) {
  if (!destination || !source) {
    return;
  }

  if (source->paired) {
    destination->paired = true;
  }

  if (source->name[0]) {
    strncpy(destination->name, source->name, sizeof(destination->name) - 1);
    destination->name[sizeof(destination->name) - 1] = '\0';
  }

  if (source->internal[0]) {
    strncpy(destination->internal, source->internal,
        sizeof(destination->internal) - 1);
    destination->internal[sizeof(destination->internal) - 1] = '\0';
  }

  if (source->external[0]) {
    strncpy(destination->external, source->external,
        sizeof(destination->external) - 1);
    destination->external[sizeof(destination->external) - 1] = '\0';
  }

  if (source->mac[0]) {
    strncpy(destination->mac, source->mac,
        sizeof(destination->mac) - 1);
    destination->mac[sizeof(destination->mac) - 1] = '\0';
  }

  destination->port = source->port != 0 ? source->port : destination->port;
  destination->prefer_external = source->prefer_external;
}

static void normalize_known_devices(void) {
  int unique_count = 0;

  for (int i = 0; i < known_devices.count; ++i) {
    device_info_t source = known_devices.devices[i];
    int match = -1;

    for (int j = 0; j < unique_count; ++j) {
      if (devices_same_identity(&known_devices.devices[j], &source)) {
        match = j;
        break;
      }
    }

    if (match < 0) {
      if (i != unique_count) {
        known_devices.devices[unique_count] = source;
      }
      ++unique_count;
      continue;
    }

    device_info_t *current = &known_devices.devices[match];
    if (should_keep_device(&source, current)) {
      device_info_t old = *current;
      *current = source;
      merge_device_info(current, &old);
      vita_debug_log(
          "normalize_known_devices: selected %s over %s",
          current->storage_name,
          old.storage_name);
    } else {
      merge_device_info(current, &source);
      vita_debug_log(
          "normalize_known_devices: merged duplicate %s into %s",
          source.storage_name,
          current->storage_name);
    }
  }

  known_devices.count = unique_count;

  for (int i = 0; i < known_devices.count; ++i) {
    ensure_host_identity(&known_devices.devices[i]);
    save_device_info(&known_devices.devices[i]);
  }
}

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
    ensure_host_identity(&info);
    append_device(&info);
  } while(true);

  sceIoDclose(dfd);

  normalize_known_devices();

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
