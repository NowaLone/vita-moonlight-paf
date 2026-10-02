#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/rng.h>

#include "moonlight/api.h"
#include "moonlight/settings.h"
#include "legacy_host_discovery.h"
#include "backend/legacy_device_store.h"
#include "legacy_gamestream.h"
#include "config.h"
#include "debug.h"

namespace {

static MoonlightEventCallback s_callback = NULL;
static void *s_userdata = NULL;
static MoonlightHost s_current_host = {0};
static bool s_has_current_host = false;
static MoonlightConnectionState s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
static LegacyGameStreamServer s_server;
static bool s_game_stream_initialized = false;
static MoonlightApplication s_applications[8];
static int s_application_count = 0;
static bool s_applications_valid = false;

static void emit(MoonlightEventType type,
                 int result,
                 int host_id,
                 int application_id,
                 const char *address)
{
    if (!s_callback) return;

    MoonlightEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.result = result;
    event.host_id = host_id;
    event.application_id = application_id;
    event.address = address;
    s_callback(&event, s_userdata);
}

static void emit_pairing_required(const char pin[5])
{
    if (!s_callback) return;

    MoonlightEvent event;
    memset(&event, 0, sizeof(event));
    event.type = MOONLIGHT_EVENT_PAIRING_REQUIRED;
    event.result = 0;
    event.host_id = s_current_host.id;
    event.application_id = -1;
    event.address = s_current_host.internal;
    memcpy(event.pairing_pin, pin, 5);
    s_callback(&event, s_userdata);
}

static void on_discovery_event(
    LegacyHostDiscoveryEventType type,
    const MoonlightHost *host,
    void *userdata)
{
    (void)userdata;

    if (type == LEGACY_HOST_DISCOVERY_FOUND) {
        if (!host) return;

        emit(
            MOONLIGHT_EVENT_HOSTS_CHANGED,
            0,
            host->id,
            -1,
            host->internal
        );
        return;
    }

    if (type == LEGACY_HOST_DISCOVERY_FINISHED) {
        emit(
            MOONLIGHT_EVENT_HOST_SCAN_FINISHED,
            0,
            -1,
            -1,
            NULL
        );
    }
}

static int make_key_directory(const MoonlightHost *host, char *out, size_t size)
{
    const char *name;

    if (!host || !out || size == 0) {
        return -1;
    }

    name = host->name[0] ? host->name : host->internal;

    int length = snprintf(out, size, "%s%s", config.key_dir, name);
    if (length < 0 || (size_t)length >= size) {
        return -1;
    }

    int result = sceIoMkdir(out, 0777);
    if (result < 0 && result != 0x80010011) {
        return result;
    }

    return 0;
}

static void update_host_from_server()
{
    s_current_host.paired = s_server.paired ? 1 : 0;

    if (s_server.mac[0]) {
        strncpy(s_current_host.mac, s_server.mac, sizeof(s_current_host.mac) - 1);
        s_current_host.mac[sizeof(s_current_host.mac) - 1] = '\0';
    }
}

}

int moonlight_api_init(void)
{
    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
    s_game_stream_initialized = false;
    memset(&s_server, 0, sizeof(s_server));

    return legacy_host_discovery_init(on_discovery_event, NULL);
}

void moonlight_api_shutdown(void)
{
    legacy_gamestream_shutdown(&s_server);
    s_game_stream_initialized = false;

    legacy_host_discovery_shutdown();

    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    s_has_current_host = false;
    memset(&s_server, 0, sizeof(s_server));
    memset(&s_current_host, 0, sizeof(s_current_host));
    s_callback = NULL;
    s_userdata = NULL;
}

int moonlight_api_open_settings(void)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_open();
}

int moonlight_api_get_settings(MoonlightSettings *out)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_get_all(out);
}

int moonlight_api_get_setting_value(MoonlightSettingKey key, int *out_value)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_get_value(key, out_value);
}

int moonlight_api_set_setting_value(MoonlightSettingKey key, int value)
{
    if (moonlight_settings_init() != 0) return -1;
    return moonlight_settings_set_value(key, value);
}

int moonlight_api_set_event_callback(MoonlightEventCallback callback, void *userdata)
{
    s_callback = callback;
    s_userdata = userdata;
    return 0;
}

int moonlight_api_get_hosts(MoonlightHost *out, int capacity)
{
    return legacy_device_store_get_hosts(out, capacity);
}

int moonlight_api_get_discovered_hosts(MoonlightHost *out, int capacity)
{
    return legacy_host_discovery_get_hosts(out, capacity);
}

int moonlight_api_search_hosts(void)
{
    emit(MOONLIGHT_EVENT_HOST_SCAN_STARTED, 0, -1, -1, NULL);

    int result = legacy_host_discovery_start();
    if (result != 0) {
        emit(MOONLIGHT_EVENT_HOST_SCAN_FAILED, result, -1, -1, NULL);
    }

    return result;
}

int moonlight_api_stop_host_search(void)
{
    return legacy_host_discovery_stop();
}

int moonlight_api_add_host(const char *address, uint16_t port, const char *name)
{
    return legacy_device_store_add_host(address, port, name);
}

int moonlight_api_connect_host(const MoonlightHost *host)
{
    char key_directory[512];

    if (!host || !host->internal[0]) {
        return -1;
    }

    if (make_key_directory(host, key_directory, sizeof(key_directory)) != 0) {
        emit(
            MOONLIGHT_EVENT_CONNECTION_FAILED,
            -1,
            host->id,
            -1,
            host->internal);
        return -1;
    }

    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }

    int result = legacy_gamestream_init(
        &s_server,
        host->internal,
        host->port,
        key_directory,
        0,
        true);

    if (result != LEGACY_GAMESTREAM_OK) {
        vita_debug_log(
            "[GameStream] init failed for %s: %d (%s)",
            host->internal,
            result,
            legacy_gamestream_error());

        s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
        s_has_current_host = false;

        emit(
            MOONLIGHT_EVENT_CONNECTION_FAILED,
            result,
            host->id,
            -1,
            host->internal);
        return result;
    }

    s_game_stream_initialized = true;
    s_current_host = *host;
    update_host_from_server();
    s_has_current_host = true;

    s_connection_state = s_server.paired
        ? MOONLIGHT_CONNECTION_PAIRED
        : MOONLIGHT_CONNECTION_READY;

    emit(
        MOONLIGHT_EVENT_CONNECTION_READY,
        0,
        s_current_host.id,
        -1,
        s_current_host.internal);

    return 0;
}

int moonlight_api_prepare_pairing(char out_pin[5])
{
    uint32_t random_value;

    if (!out_pin || !s_has_current_host ||
        s_connection_state != MOONLIGHT_CONNECTION_READY ||
        !s_game_stream_initialized) {
        return -1;
    }

    if (s_server.paired) {
        s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
        return LEGACY_GAMESTREAM_WRONG_STATE;
    }

    if (sceKernelGetRandomNumber(&random_value, sizeof(random_value)) < 0) {
        return -1;
    }

    snprintf(out_pin, 5, "%04u", (unsigned)(random_value % 10000));

    emit_pairing_required(out_pin);
    return 0;
}

int moonlight_api_pair_current_host(const char pin[5])
{
    int result;

    if (!pin || !s_has_current_host ||
        s_connection_state != MOONLIGHT_CONNECTION_READY ||
        !s_game_stream_initialized) {
        return -1;
    }

    result = legacy_gamestream_pair(&s_server, pin);
    if (result != LEGACY_GAMESTREAM_OK) {
        vita_debug_log(
            "[GameStream] pairing failed for %s: %d (%s)",
            s_current_host.internal,
            result,
            legacy_gamestream_error());

        emit(
            MOONLIGHT_EVENT_PAIRING_FAILED,
            result,
            s_current_host.id,
            -1,
            s_current_host.internal);
        return result;
    }

    update_host_from_server();
    s_connection_state = MOONLIGHT_CONNECTION_PAIRED;

    int save_result = legacy_device_store_mark_paired(&s_current_host);
    if (save_result != 0) {
        vita_debug_log(
            "[GameStream] pairing succeeded but device persistence failed: %d",
            save_result);
    }

    emit(
        MOONLIGHT_EVENT_PAIRING_FINISHED,
        save_result,
        s_current_host.id,
        -1,
        s_current_host.internal);

    return 0;
}

int moonlight_api_get_applications(MoonlightApplication *out, int capacity)
{
    if (out && capacity > 0) {
        int count = s_application_count;
        if (count > capacity) count = capacity;
        if (count > 0) {
            memcpy(out, s_applications, sizeof(MoonlightApplication) * count);
        }
        return s_applications_valid ? count : -1;
    }

    if (!s_has_current_host || !s_game_stream_initialized || !s_server.curl ||
        (s_connection_state != MOONLIGHT_CONNECTION_PAIRED &&
         s_connection_state != MOONLIGHT_CONNECTION_STREAMING &&
         s_connection_state != MOONLIGHT_CONNECTION_PAUSED)) {
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, -1, s_current_host.id, -1, s_current_host.internal);
        return -1;
    }

    char unique_id[32];
    char uuid[40];
    char *url;
    char *response;
    unsigned char random_bytes[16];
    FILE *unique_file;
    char unique_path[512];
    const char *name = s_current_host.name[0] ? s_current_host.name : s_current_host.internal;
    int written;

    unique_id[0] = '\0';
    written = snprintf(unique_path, sizeof(unique_path), "%s%s/uniqueid.dat", config.key_dir, name);
    unique_file = (written > 0 && (size_t)written < sizeof(unique_path)) ? fopen(unique_path, "rb") : NULL;
    if (unique_file) {
        if (fread(unique_id, 1, 16, unique_file) == 16) {
            unique_id[16] = '\0';
        }
        fclose(unique_file);
    }
    if (!unique_id[0]) {
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, -2, s_current_host.id, -1, s_current_host.internal);
        return -2;
    }

    if (sceKernelGetRandomNumber(random_bytes, sizeof(random_bytes)) < 0) {
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, -3, s_current_host.id, -1, s_current_host.internal);
        return -3;
    }
    snprintf(uuid, sizeof(uuid),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
             random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7],
             random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11],
             random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);

    url = (char *)malloc(2048);
    response = (char *)malloc(1);
    if (!url || !response) {
        free(url);
        free(response);
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, -4, s_current_host.id, -1, s_current_host.internal);
        return -4;
    }
    response[0] = '\0';

    snprintf(url, 2048,
             "https://%s:%u/applist?uniqueid=%s&uuid=%s",
             s_server.address,
             s_server.https_port ? s_server.https_port : 47984,
             unique_id,
             uuid);

    struct AppListBuffer {
        char *memory;
        size_t size;
    } buffer;
    buffer.memory = response;
    buffer.size = 0;

    curl_easy_setopt((CURL *)s_server.curl, CURLOPT_URL, url);
    curl_easy_setopt((CURL *)s_server.curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt((CURL *)s_server.curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt((CURL *)s_server.curl, CURLOPT_TIMEOUT, 20L);

    vita_debug_log("[GameStream] requesting applist %s", s_server.address);
    CURLcode curl_result = curl_easy_perform((CURL *)s_server.curl);
    free(url);
    response = buffer.memory;

    if (curl_result != CURLE_OK || !response) {
        vita_debug_log("[GameStream] applist failed: %s", curl_easy_strerror(curl_result));
        free(response);
        s_applications_valid = false;
        s_application_count = 0;
        emit(MOONLIGHT_EVENT_APPLICATIONS_FAILED, (int)curl_result, s_current_host.id, -1, s_current_host.internal);
        return -1;
    }

    s_application_count = 0;
    const char *cursor = response;
    char title[256];
    int id = 0;
    bool have_title = false;
    bool have_id = false;
    title[0] = '\0';

    while (cursor && *cursor && s_application_count < 8) {
        const char *tag = strchr(cursor, '<');
        if (!tag) break;

        if (strncmp(tag, "<AppTitle>", 10) == 0) {
            const char *value = tag + 10;
            const char *end = strstr(value, "</AppTitle>");
            size_t length = end ? (size_t)(end - value) : 0;
            if (length >= sizeof(title)) length = sizeof(title) - 1;
            memcpy(title, value, length);
            title[length] = '\0';
            have_title = true;
            cursor = end ? end + 11 : value;
        } else if (strncmp(tag, "<ID>", 4) == 0) {
            id = atoi(tag + 4);
            have_id = true;
            cursor = tag + 4;
        } else if (strncmp(tag, "</App>", 6) == 0) {
            if (have_title && have_id) {
                MoonlightApplication *app = &s_applications[s_application_count++];
                memset(app, 0, sizeof(*app));
                app->id = id;
                strncpy(app->name, title, sizeof(app->name) - 1);
            }
            have_title = false;
            have_id = false;
            title[0] = '\0';
            cursor = tag + 6;
        } else {
            cursor = tag + 1;
        }
    }

    free(response);
    s_applications_valid = true;
    vita_debug_log("[GameStream] applist count %d", s_application_count);
    emit(MOONLIGHT_EVENT_APPLICATIONS_READY, s_application_count, s_current_host.id, -1, s_current_host.internal);
    return s_application_count;
}

int moonlight_api_start_application(int application_id)
{
    if (!s_has_current_host || s_connection_state != MOONLIGHT_CONNECTION_PAIRED) {
        return -1;
    }

    s_connection_state = MOONLIGHT_CONNECTION_STREAMING;
    emit(MOONLIGHT_EVENT_STREAM_STARTED,
         0,
         s_current_host.id,
         application_id,
         s_current_host.internal);
    return 0;
}

int moonlight_api_stop_application(void)
{
    if (!s_has_current_host || s_connection_state != MOONLIGHT_CONNECTION_STREAMING) {
        return -1;
    }

    s_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    emit(MOONLIGHT_EVENT_STREAM_STOPPED, 0, s_current_host.id, -1, s_current_host.internal);
    return 0;
}

int moonlight_api_disconnect_host(void)
{
    if (!s_has_current_host) {
        return 0;
    }

    if (s_game_stream_initialized) {
        legacy_gamestream_shutdown(&s_server);
        s_game_stream_initialized = false;
    }

    s_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    emit(MOONLIGHT_EVENT_CONNECTION_CLOSED,
         0,
         s_current_host.id,
         -1,
         s_current_host.internal);

    s_has_current_host = false;
    return 0;
}

MoonlightConnectionState moonlight_api_get_connection_state(void)
{
    return s_connection_state;
}
