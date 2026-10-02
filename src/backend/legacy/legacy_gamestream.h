#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEGACY_GAMESTREAM_OK 0
#define LEGACY_GAMESTREAM_FAILED -1
#define LEGACY_GAMESTREAM_OUT_OF_MEMORY -2
#define LEGACY_GAMESTREAM_INVALID -3
#define LEGACY_GAMESTREAM_WRONG_STATE -4
#define LEGACY_GAMESTREAM_IO_ERROR -5
#define LEGACY_GAMESTREAM_UNSUPPORTED_VERSION -7

typedef struct LegacyGameStreamServer {
    char address[256];
    unsigned short http_port;
    unsigned short https_port;
    int server_major_version;
    int current_game;
    bool paired;
    bool unsupported;
    char mac[18];
    void *curl;
} LegacyGameStreamServer;

int legacy_gamestream_init(
    LegacyGameStreamServer *server,
    const char *address,
    unsigned short http_port,
    const char *key_directory,
    int log_level,
    bool unsupported);

int legacy_gamestream_pair(
    LegacyGameStreamServer *server,
    const char *pin);

int legacy_gamestream_launch(
    LegacyGameStreamServer *server,
    int app_id,
    char *session_url,
    size_t session_url_size);

int legacy_gamestream_get_server_mac(
    LegacyGameStreamServer *server,
    char *mac,
    unsigned int size);

void legacy_gamestream_shutdown(LegacyGameStreamServer *server);

const char *legacy_gamestream_error(void);

#ifdef __cplusplus
}
#endif
