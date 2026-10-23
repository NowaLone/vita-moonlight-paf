#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "moonlight/cancellation.h"

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
#define LEGACY_GAMESTREAM_APP_CONFLICT -8
#define LEGACY_GAMESTREAM_NOT_PAIRED -9
#define LEGACY_GAMESTREAM_SECURITY_ERROR -10
#define LEGACY_GAMESTREAM_CANCELLED -11
/* Local execution failure and authenticated PIN mismatch, never inferred from log text. */
#define LEGACY_GAMESTREAM_INTERNAL_ERROR -12
#define LEGACY_GAMESTREAM_INCORRECT_PIN -13
#define LEGACY_GAMESTREAM_QUIT_FAILED -14

typedef struct LegacyGameStreamServer {
    char address[256];
    char unique_id[64];
    unsigned short http_port;
    unsigned short https_port;
    int server_major_version;
    char server_info_app_version[64];
    char server_info_gfe_version[64];
    char rtsp_session_url[256];
    int server_codec_mode_support;
    int current_game;
    bool paired;
    bool unsupported;
    char mac[18];
    char server_certificate_path[512];
    void *server_certificate; /* X509, owned by this server, never a client key. */
    void *curl;
} LegacyGameStreamServer;

typedef struct LegacyGameStreamPairingError {
    int status_code; /* GameStream XML status, not HTTP transport status; zero if absent. */
    char status_message[256];
} LegacyGameStreamPairingError;

int legacy_gamestream_init(
    LegacyGameStreamServer *server,
    const char *address,
    unsigned short http_port,
    const char *client_directory,
    const char *server_certificate_path,
    bool unsupported);

int legacy_gamestream_pair(
    LegacyGameStreamServer *server,
    const char *pin,
    LegacyGameStreamPairingError *error,
    const MoonlightCancellation *cancel);

/* Authenticated snapshot only; never fall back to plaintext when starting a stream. */
int legacy_gamestream_refresh_server_info(LegacyGameStreamServer *server);

/* Worker only, after explicit confirmation: quits the host app and all remote sessions. */
int legacy_gamestream_quit_app(LegacyGameStreamServer *server);

/* Assigns only the persistence destination for a newly saved host, never establishes trust. */
int legacy_gamestream_set_certificate_path(LegacyGameStreamServer *server, const char *path);

/* Caller owns the returned body; timeout and response callback are scoped to the request. */
int legacy_gamestream_request(
    LegacyGameStreamServer *server, const char *endpoint, const char *query,
    long timeout_seconds, char **body, size_t *length);

void legacy_gamestream_shutdown(LegacyGameStreamServer *server);

const char *legacy_gamestream_error(void);
int legacy_gamestream_last_http_error(void);

#ifdef __cplusplus
}
#endif
