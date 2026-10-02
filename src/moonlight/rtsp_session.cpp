#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>

#include "debug.h"

namespace {

static const int kEagain = (int)0x80410123;

static const char *skip_space(const char *text)
{
    while (text && (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t')) ++text;
    return text;
}

static int parse_rtsp_url(const char *url, char *host, size_t host_size, unsigned short *port)
{
    const char *start;
    const char *colon;
    const char *slash;
    size_t length;

    url = skip_space(url);
    if (!url) return -1;
    if (strncmp(url, "rtsp://", 7) == 0) start = url + 7;
    else if (strncmp(url, "rtspenc://", 10) == 0) start = url + 10;
    else return -1;

    slash = strchr(start, '/');
    colon = strchr(start, ':');
    if (colon && (!slash || colon < slash)) {
        length = (size_t)(colon - start);
        *port = (unsigned short)atoi(colon + 1);
    } else {
        length = slash ? (size_t)(slash - start) : strlen(start);
        *port = 48010;
    }
    if (length == 0 || length >= host_size || *port == 0) return -1;
    memcpy(host, start, length);
    host[length] = '\0';
    return 0;
}

static int content_length(const char *response)
{
    const char *marker = response ? strstr(response, "Content-Length:") : NULL;
    if (!marker) marker = response ? strstr(response, "Content-length:") : NULL;
    return marker ? atoi(marker + 15) : 0;
}

static char *header_end(char *response)
{
    char *end = strstr(response, "\r\n\r\n");
    return end ? end : strstr(response, "\n\n");
}

static int rtsp_exchange(int sock, const char *request, char *response, size_t response_size)
{
    int sent = 0;
    int length = (int)strlen(request);
    int received = 0;
    int waited = 0;
    char *end;
    int body;
    int marker;

    response[0] = '\0';
    while (sent < length) {
        int result = sceNetSend(sock, request + sent, length - sent, 0);
        if (result == kEagain || result == -kEagain) {
            sceKernelDelayThread(20 * 1000);
            continue;
        }
        if (result <= 0) return result;
        sent += result;
    }

    while (received < (int)response_size - 1 && waited < 8000) {
        int result = sceNetRecv(sock, response + received, response_size - 1 - received, SCE_NET_MSG_DONTWAIT);
        if (result == kEagain || result == -kEagain || result == 0) {
            sceKernelDelayThread(50 * 1000);
            waited += 50;
            continue;
        }
        if (result < 0) return received > 0 ? received : result;
        received += result;
        response[received] = '\0';
        end = header_end(response);
        if (!end) continue;
        marker = (end[0] == '\r') ? 4 : 2;
        body = content_length(response);
        if (body < 0) body = 0;
        if (received >= (int)(end - response) + marker + body) break;
    }
    return received > 0 ? received : -1;
}

static int status_code(const char *response)
{
    const char *space = response ? strchr(response, ' ') : NULL;
    return space ? atoi(space + 1) : 0;
}

static int server_port(const char *response)
{
    const char *marker = response ? strstr(response, "server_port=") : NULL;
    return marker ? atoi(marker + 12) : 0;
}

static int connect_rtsp(const char *host, unsigned short port, int *out_error)
{
    SceNetSockaddrIn address;
    int sock;
    int result;
    int attempt;

    *out_error = 0;
    for (attempt = 0; attempt < 8; ++attempt) {
        sock = sceNetSocket("rtsp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
        if (sock < 0) {
            *out_error = sock;
            return -1;
        }

        memset(&address, 0, sizeof(address));
        address.sin_len = sizeof(address);
        address.sin_family = SCE_NET_AF_INET;
        address.sin_port = sceNetHtons(port);
        result = sceNetInetPton(SCE_NET_AF_INET, host, &address.sin_addr);
        if (result <= 0) {
            sceNetSocketClose(sock);
            *out_error = result;
            return -2;
        }
        result = sceNetConnect(sock, (SceNetSockaddr *)&address, sizeof(address));
        if (result >= 0) return sock;

        *out_error = result;
        sceNetSocketClose(sock);
        sceKernelDelayThread(250 * 1000);
    }
    return -1;
}

static void fail_step(char *status, size_t status_size, int sock, const char *step, const char *response, int result)
{
    if (response && response[0]) {
        snprintf(status, status_size, "RTSP %s %d", step, status_code(response));
    } else {
        snprintf(status, status_size, "RTSP %s NO REPLY %d", step, result);
    }
    vita_debug_log("[GameStream] %s", status);
    sceNetSocketClose(sock);
}

}

extern "C" int moonlight_rtsp_start(const char *session_url, char *status, size_t status_size)
{
    char host[128];
    unsigned short port = 0;
    int sock;
    int error = 0;
    int result;
    char request[1024];
    char response[8192];
    int video_port = 0;

    if (!status || status_size == 0) return -1;
    snprintf(status, status_size, "RTSP FAIL");
    if (parse_rtsp_url(session_url, host, sizeof(host), &port) != 0) {
        snprintf(status, status_size, "RTSP BAD URL");
        vita_debug_log("[GameStream] bad session url: %.80s", session_url ? session_url : "empty");
        return -1;
    }

    sock = connect_rtsp(host, port, &error);
    if (sock < 0) {
        snprintf(status, status_size, "RTSP CONNECT %s:%u %d", host, port, error);
        vita_debug_log("[GameStream] %s", status);
        return -1;
    }

    snprintf(request, sizeof(request),
             "OPTIONS rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 1\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    result = rtsp_exchange(sock, request, response, sizeof(response));
    if (result < 0 || status_code(response) != 200) {
        fail_step(status, status_size, sock, "OPTIONS", response, result);
        return -1;
    }

    snprintf(request, sizeof(request),
             "DESCRIBE rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 2\r\n"
             "User-Agent: Moonlight/4.3.1\r\n"
             "Accept: application/sdp\r\n"
             "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    result = rtsp_exchange(sock, request, response, sizeof(response));
    if (result < 0 || status_code(response) != 200) {
        fail_step(status, status_size, sock, "DESCRIBE", response, result);
        return -1;
    }

    snprintf(request, sizeof(request),
             "SETUP rtsp://%s:%u/streamid=video/0/0 RTSP/1.0\r\n"
             "CSeq: 3\r\n"
             "Transport: RTP/AVP/UDP;unicast;client_port=47998-47999\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    result = rtsp_exchange(sock, request, response, sizeof(response));
    if (result < 0 || status_code(response) != 200) {
        fail_step(status, status_size, sock, "SETUP", response, result);
        return -1;
    }
    video_port = server_port(response);

    snprintf(request, sizeof(request),
             "PLAY rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 4\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    result = rtsp_exchange(sock, request, response, sizeof(response));
    if (result < 0 || status_code(response) != 200) {
        fail_step(status, status_size, sock, "PLAY", response, result);
        return -1;
    }

    sceNetSocketClose(sock);
    snprintf(status, status_size, "RTSP PLAY v%d", video_port);
    vita_debug_log("[GameStream] %s", status);
    return 0;
}
