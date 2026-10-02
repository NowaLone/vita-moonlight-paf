#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <psp2/kernel/rng.h>

#include "debug.h"

extern "C" int moonlight_rtsp_start(const char *session_url, char *status, size_t status_size);

namespace {

struct LaunchCall {
    void *curl;
    const char *address;
    unsigned short https_port;
    const char *unique_path;
    int app_id;
    char session_url[256];
    int result;
};

struct LaunchBuffer {
    char *memory;
    size_t size;
};

static size_t launch_write(void *contents, size_t size, size_t count, void *userdata)
{
    LaunchBuffer *buffer = (LaunchBuffer *)userdata;
    size_t length = size * count;
    char *memory = (char *)realloc(buffer->memory, buffer->size + length + 1);
    if (!memory) return 0;
    buffer->memory = memory;
    memcpy(buffer->memory + buffer->size, contents, length);
    buffer->size += length;
    buffer->memory[buffer->size] = '\0';
    return length;
}

static void *launch_thread(void *argument)
{
    LaunchCall *call = (LaunchCall *)argument;
    char unique_id[32];
    char uuid[40];
    char rikey[33];
    unsigned char random_bytes[16];
    unsigned int rikey_id = 0;
    char *url;
    FILE *unique_file;
    LaunchBuffer buffer;
    char rtsp_status[64];

    call->result = -1;
    call->session_url[0] = '\0';
    unique_id[0] = '\0';
    unique_file = call->unique_path ? fopen(call->unique_path, "rb") : NULL;
    if (unique_file) {
        if (fread(unique_id, 1, 16, unique_file) == 16) unique_id[16] = '\0';
        fclose(unique_file);
    }
    if (!unique_id[0] || !call->curl) {
        call->result = -2;
        return NULL;
    }
    if (sceKernelGetRandomNumber(random_bytes, sizeof(random_bytes)) < 0 ||
        sceKernelGetRandomNumber(&rikey_id, sizeof(rikey_id)) < 0) {
        call->result = -3;
        return NULL;
    }
    snprintf(uuid, sizeof(uuid),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
             random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7],
             random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11],
             random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);
    snprintf(rikey, sizeof(rikey),
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
             random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7],
             random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11],
             random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);

    url = (char *)malloc(2048);
    buffer.memory = (char *)malloc(1);
    if (!url || !buffer.memory) {
        free(url);
        free(buffer.memory);
        call->result = -4;
        return NULL;
    }
    buffer.memory[0] = '\0';
    buffer.size = 0;
    snprintf(url, 2048,
             "https://%s:%u/launch?uniqueid=%s&uuid=%s&appid=%d&mode=1280x720x60&additional=0&rikey=%s&rikeyid=%u&localAudioPlayMode=0&surroundAudioInfo=197322&remoteControllersBitmap=1&gcmap=1&sops=1&corever=1",
             call->address,
             call->https_port ? call->https_port : 47984,
             unique_id,
             uuid,
             call->app_id,
             rikey,
             rikey_id);
    curl_easy_setopt((CURL *)call->curl, CURLOPT_URL, url);
    curl_easy_setopt((CURL *)call->curl, CURLOPT_WRITEFUNCTION, launch_write);
    curl_easy_setopt((CURL *)call->curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt((CURL *)call->curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt((CURL *)call->curl, CURLOPT_TIMEOUT, 20L);
    vita_debug_log("[GameStream] launching app %d on %s", call->app_id, call->address);
    CURLcode curl_result = curl_easy_perform((CURL *)call->curl);
    free(url);
    if (curl_result != CURLE_OK || !buffer.memory || !strstr(buffer.memory, "status_code=\"200\"")) {
        vita_debug_log("[GameStream] launch rejected: %.180s", buffer.memory ? buffer.memory : curl_easy_strerror(curl_result));
        free(buffer.memory);
        call->result = curl_result != CURLE_OK ? (int)curl_result : -5;
        return NULL;
    }
    const char *session = strstr(buffer.memory, "<sessionUrl0>");
    if (session) {
        session += 13;
        const char *end = strstr(session, "</sessionUrl0>");
        size_t length = end ? (size_t)(end - session) : 0;
        if (length >= sizeof(call->session_url)) length = sizeof(call->session_url) - 1;
        memcpy(call->session_url, session, length);
        call->session_url[length] = '\0';
    }
    vita_debug_log("[GameStream] launch ok session %s", call->session_url[0] ? call->session_url : "none");
    free(buffer.memory);
    rtsp_status[0] = '\0';
    moonlight_rtsp_start(call->session_url, rtsp_status, sizeof(rtsp_status));
    if (rtsp_status[0]) {
        strncpy(call->session_url, rtsp_status, sizeof(call->session_url) - 1);
        call->session_url[sizeof(call->session_url) - 1] = '\0';
    }
    call->result = 0;
    return NULL;
}

}

extern "C" int moonlight_launch_request(
    void *curl,
    const char *address,
    unsigned short https_port,
    const char *unique_path,
    int app_id,
    char *session_url,
    size_t session_url_size)
{
    LaunchCall call;
    pthread_t thread;
    pthread_attr_t attr;

    memset(&call, 0, sizeof(call));
    call.curl = curl;
    call.address = address;
    call.https_port = https_port;
    call.unique_path = unique_path;
    call.app_id = app_id;
    call.result = -1;
    if (pthread_attr_init(&attr) != 0) return -1;
    if (pthread_attr_setstacksize(&attr, 256 * 1024) != 0 ||
        pthread_create(&thread, &attr, launch_thread, &call) != 0) {
        pthread_attr_destroy(&attr);
        return -1;
    }
    pthread_attr_destroy(&attr);
    pthread_join(thread, NULL);
    if (session_url && session_url_size > 0) {
        strncpy(session_url, call.session_url, session_url_size - 1);
        session_url[session_url_size - 1] = '\0';
    }
    return call.result;
}
