#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/net/net.h>

#include "debug.h"

namespace {

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

static int rtsp_exchange(int sock, const char *request, char *response, size_t response_size)
{
    int sent = 0;
    int length = (int)strlen(request);
    int received = 0;

    while (sent < length) {
        int result = sceNetSend(sock, request + sent, length - sent, 0);
        if (result <= 0) return -1;
        sent += result;
    }

    while (received < (int)response_size - 1) {
        int result = sceNetRecv(sock, response + received, response_size - 1 - received, 0);
        if (result <= 0) break;
        received += result;
        response[received] = '\0';
        if (strstr(response, "\r\n\r\n")) break;
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

}

extern "C" int moonlight_rtsp_start(const char *session_url, char *status, size_t status_size)
{
    char host[128];
    unsigned short port = 0;
    SceNetSockaddrIn address;
    int sock;
    int timeout = 5000;
    char request[1024];
    char response[2048];
    int code;
    int video_port = 0;

    if (!status || status_size == 0) return -1;
    snprintf(status, status_size, "RTSP FAIL");
    if (parse_rtsp_url(session_url, host, sizeof(host), &port) != 0) {
        snprintf(status, status_size, "RTSP BAD URL");
        vita_debug_log("[GameStream] bad session url: %.80s", session_url ? session_url : "empty");
        return -1;
    }

    sock = sceNetSocket("rtsp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(status, status_size, "RTSP SOCKET");
        return -1;
    }
    sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout, sizeof(timeout));
    sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout, sizeof(timeout));

    memset(&address, 0, sizeof(address));
    address.sin_family = SCE_NET_AF_INET;
    address.sin_port = sceNetHtons(port);
    if (sceNetInetPton(SCE_NET_AF_INET, host, &address.sin_addr) <= 0 ||
        sceNetConnect(sock, (SceNetSockaddr *)&address, sizeof(address)) < 0) {
        sceNetSocketClose(sock);
        snprintf(status, status_size, "RTSP CONNECT");
        return -1;
    }

    snprintf(request, sizeof(request),
             "OPTIONS rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 1\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    if (rtsp_exchange(sock, request, response, sizeof(response)) < 0 || status_code(response) != 200) {
        sceNetSocketClose(sock);
        snprintf(status, status_size, "RTSP OPTIONS %d", status_code(response));
        return -1;
    }

    snprintf(request, sizeof(request),
             "DESCRIBE rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 2\r\n"
             "Accept: application/sdp\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    if (rtsp_exchange(sock, request, response, sizeof(response)) < 0 || status_code(response) != 200) {
        sceNetSocketClose(sock);
        snprintf(status, status_size, "RTSP DESCRIBE %d", status_code(response));
        return -1;
    }

    snprintf(request, sizeof(request),
             "SETUP rtsp://%s:%u/streamid=video/0/0 RTSP/1.0\r\n"
             "CSeq: 3\r\n"
             "Transport: RTP/AVP/UDP;unicast;client_port=47998-47999\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    if (rtsp_exchange(sock, request, response, sizeof(response)) < 0 ||
        (code = status_code(response)) != 200) {
        sceNetSocketClose(sock);
        snprintf(status, status_size, "RTSP SETUP %d", status_code(response));
        return -1;
    }
    video_port = server_port(response);

    snprintf(request, sizeof(request),
             "PLAY rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 4\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n\r\n",
             host, port, host);
    if (rtsp_exchange(sock, request, response, sizeof(response)) < 0 || status_code(response) != 200) {
        sceNetSocketClose(sock);
        snprintf(status, status_size, "RTSP PLAY %d", status_code(response));
        return -1;
    }

    sceNetSocketClose(sock);
    snprintf(status, status_size, "RTSP PLAY v%d", video_port);
    vita_debug_log("[GameStream] %s", status);
    return 0;
}
