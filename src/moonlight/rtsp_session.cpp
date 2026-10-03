#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/rng.h>
#include <psp2/net/net.h>

#include "debug.h"

namespace {

static const int kReset = (int)0x80410136;
static const int kEagain = (int)0x80410123;
static const unsigned short kVideoPort = 47998;
static const unsigned short kAudioPort = 48000;
static const unsigned short kControlPort = 47999;
static const unsigned short kEnetPeerIdMaximum = 0x0fff;
static const unsigned short kEnetHeaderSentTime = 0x8000;
static const unsigned char kEnetCommandAcknowledge = 1;
static const unsigned char kEnetCommandConnect = 2;
static const unsigned char kEnetCommandVerifyConnect = 3;
static const unsigned char kEnetCommandFlagAcknowledge = 0x80;
static const unsigned char kEnetChannelCount = 0x30;
static const unsigned int kEnetMtu = 1392;
static const unsigned int kEnetWindowSize = 65536;
static const unsigned int kEnetThrottleInterval = 5000;
static const unsigned int kEnetThrottleAcceleration = 2;
static const unsigned int kEnetThrottleDeceleration = 2;

static unsigned short read_be16(const unsigned char *data)
{
    return (unsigned short)(((unsigned short)data[0] << 8) | data[1]);
}

static unsigned int read_be32(const unsigned char *data)
{
    return ((unsigned int)data[0] << 24) |
           ((unsigned int)data[1] << 16) |
           ((unsigned int)data[2] << 8) |
           (unsigned int)data[3];
}

static void write_be16(unsigned char *data, unsigned short value)
{
    data[0] = (unsigned char)(value >> 8);
    data[1] = (unsigned char)value;
}

static void write_be32(unsigned char *data, unsigned int value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static void write_host32(unsigned char *data, unsigned int value)
{
    memcpy(data, &value, sizeof(value));
}

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

static int is_again(int result)
{
    return result == kEagain || result == -kEagain;
}

static int connect_rtsp(const char *host, unsigned short port)
{
    SceNetSockaddrIn address;
    int sock;
    int result;
    int attempt;

    for (attempt = 0; attempt < 5; ++attempt) {
        sock = sceNetSocket("rtsp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
        if (sock < 0) return sock;
        memset(&address, 0, sizeof(address));
        address.sin_len = sizeof(address);
        address.sin_family = SCE_NET_AF_INET;
        address.sin_port = sceNetHtons(port);
        result = sceNetInetPton(SCE_NET_AF_INET, host, &address.sin_addr);
        if (result <= 0) {
            sceNetSocketClose(sock);
            return -2;
        }
        result = sceNetConnect(sock, (SceNetSockaddr *)&address, sizeof(address));
        if (result >= 0) return sock;
        sceNetSocketClose(sock);
        sceKernelDelayThread(150 * 1000);
    }
    return -1;
}

static int bind_udp(unsigned short port)
{
    SceNetSockaddrIn address;
    int sock = sceNetSocket("rtp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    int result;
    if (sock < 0) return sock;
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = SCE_NET_AF_INET;
    address.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
    address.sin_port = sceNetHtons(port);
    result = sceNetBind(sock, (SceNetSockaddr *)&address, sizeof(address));
    if (result < 0) {
        sceNetSocketClose(sock);
        return result;
    }
    return sock;
}

static void send_ping(int sock, const char *host, unsigned short port)
{
    SceNetSockaddrIn address;
    static const char ping[] = "PING";

    if (sock < 0 || !host || !port) return;
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = SCE_NET_AF_INET;
    address.sin_port = sceNetHtons(port);
    if (sceNetInetPton(SCE_NET_AF_INET, host, &address.sin_addr) <= 0) return;
    sceNetSendto(sock, ping, 4, 0, (SceNetSockaddr *)&address, sizeof(address));
}

static int start_enet_control(const char *host, unsigned short port, int *out_sock)
{
    unsigned char connect_packet[52];
    unsigned char verify_packet[256];
    unsigned char ack_packet[10];
    SceNetSockaddrIn address;
    SceNetSockaddrIn from;
    unsigned int from_length;
    unsigned int connect_id;
    int sock;
    int result;
    int waited;
    int header_size;
    unsigned short verify_peer_id;
    unsigned short verify_reliable_sequence;
    unsigned char outgoing_session_id;
    unsigned int packet_connect_id;

    if (!host || !out_sock) return -1;
    *out_sock = -1;

    sock = bind_udp(0);
    if (sock < 0) return sock;

    if (sceKernelGetRandomNumber(&connect_id, sizeof(connect_id)) < 0 || connect_id == 0) {
        connect_id = 0x4d4c5046U;
    }

    memset(connect_packet, 0, sizeof(connect_packet));
    write_be16(connect_packet + 0, (unsigned short)(kEnetPeerIdMaximum | kEnetHeaderSentTime));
    write_be16(connect_packet + 2, 0);
    connect_packet[4] = (unsigned char)(kEnetCommandConnect | kEnetCommandFlagAcknowledge);
    connect_packet[5] = 0xff;
    write_be16(connect_packet + 6, 1);
    write_be16(connect_packet + 8, 0);
    connect_packet[10] = 0xff;
    connect_packet[11] = 0xff;
    write_be32(connect_packet + 12, kEnetMtu);
    write_be32(connect_packet + 16, kEnetWindowSize);
    write_be32(connect_packet + 20, kEnetChannelCount);
    write_be32(connect_packet + 24, 0);
    write_be32(connect_packet + 28, 0);
    write_be32(connect_packet + 32, kEnetThrottleInterval);
    write_be32(connect_packet + 36, kEnetThrottleAcceleration);
    write_be32(connect_packet + 40, kEnetThrottleDeceleration);
    write_host32(connect_packet + 44, connect_id);
    write_be32(connect_packet + 48, 0);

    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = SCE_NET_AF_INET;
    address.sin_port = sceNetHtons(port);
    result = sceNetInetPton(SCE_NET_AF_INET, host, &address.sin_addr);
    if (result <= 0) {
        sceNetSocketClose(sock);
        return -2;
    }

    result = sceNetSendto(sock, connect_packet, sizeof(connect_packet), 0,
                          (SceNetSockaddr *)&address, sizeof(address));
    if (result != (int)sizeof(connect_packet)) {
        sceNetSocketClose(sock);
        return -3;
    }

    waited = 0;
    while (waited < 3000) {
        from_length = sizeof(from);
        memset(&from, 0, sizeof(from));
        result = sceNetRecvfrom(sock, verify_packet, sizeof(verify_packet),
                                SCE_NET_MSG_DONTWAIT,
                                (SceNetSockaddr *)&from, &from_length);
        if (result > 0) {
            unsigned short peer_flags;
            unsigned char command;
            const unsigned char *verify;

            if (result < 2) continue;
            peer_flags = read_be16(verify_packet);
            header_size = (peer_flags & kEnetHeaderSentTime) ? 4 : 2;
            if (result < header_size + 44) continue;

            verify = verify_packet + header_size;
            command = verify[0];
            if ((command & 0x0f) != kEnetCommandVerifyConnect) continue;
            if ((command & kEnetCommandFlagAcknowledge) == 0) continue;
            if (verify[1] != 0xff) continue;

            memcpy(&packet_connect_id, verify + 40, sizeof(packet_connect_id));
            if (packet_connect_id != connect_id) continue;

            verify_peer_id = (unsigned short)(read_be16(verify + 4) & kEnetPeerIdMaximum);
            outgoing_session_id = verify[7];
            verify_reliable_sequence = read_be16(verify + 2);

            memset(ack_packet, 0, sizeof(ack_packet));
            write_be16(
                ack_packet + 0,
                (unsigned short)(verify_peer_id |
                    ((unsigned short)(outgoing_session_id & 0x03) << 12)));
            ack_packet[2] = kEnetCommandAcknowledge;
            ack_packet[3] = 0xff;
            write_be16(ack_packet + 4, verify_reliable_sequence);
            write_be16(ack_packet + 6, verify_reliable_sequence);
            write_be16(
                ack_packet + 8,
                (peer_flags & kEnetHeaderSentTime) ? read_be16(verify_packet + 2) : 0);

            result = sceNetSendto(sock, ack_packet, sizeof(ack_packet), 0,
                                  (SceNetSockaddr *)&address, sizeof(address));
            if (result == (int)sizeof(ack_packet)) {
                vita_debug_log("[GameStream] ENet control connected peer=%u session=%u",
                               (unsigned int)verify_peer_id,
                               (unsigned int)(outgoing_session_id & 0x03));
                *out_sock = sock;
                sceKernelDelayThread(100 * 1000);
                return 0;
            }

            sceNetSocketClose(sock);
            return -4;
        }

        sceKernelDelayThread(20 * 1000);
        waited += 20;
    }

    vita_debug_log("[GameStream] ENet control connect timeout");
    sceNetSocketClose(sock);
    return -5;
}

static int count_packets(int sock, int audio_sock, const char *host, unsigned short video_port, int milliseconds)
{
    char packet[2048];
    SceNetSockaddrIn from;
    unsigned int from_length;
    int count = 0;
    int waited = 0;

    while (waited < milliseconds) {
        int result;
        if ((waited % 200) == 0) {
            send_ping(sock, host, video_port ? video_port : kVideoPort);
            send_ping(audio_sock, host, kAudioPort);
        }
        from_length = sizeof(from);
        memset(&from, 0, sizeof(from));
        result = sceNetRecvfrom(sock, packet, sizeof(packet), SCE_NET_MSG_DONTWAIT, (SceNetSockaddr *)&from, &from_length);
        if (result > 0) {
            ++count;
            continue;
        }
        sceKernelDelayThread(20 * 1000);
        waited += 20;
    }
    return count;
}

static int exchange(int sock, const char *request, char *response, size_t response_size)
{
    int sent = 0;
    int length = (int)strlen(request);
    int received = 0;

    response[0] = '\0';
    while (sent < length) {
        int result = sceNetSend(sock, request + sent, length - sent, 0);
        if (is_again(result)) {
            sceKernelDelayThread(20 * 1000);
            continue;
        }
        if (result <= 0) return result;
        sent += result;
    }

    while (received < (int)response_size - 1) {
        int result = sceNetRecv(sock, response + received, response_size - 1 - received, 0);
        char *end;
        int body;
        int marker;
        if (is_again(result)) {
            sceKernelDelayThread(20 * 1000);
            continue;
        }
        if (result <= 0) return received > 0 ? received : result;
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

static void copy_token(const char *response, const char *name, char *out, size_t out_size)
{
    const char *marker = response ? strstr(response, name) : NULL;
    size_t length;
    if (!marker || out_size == 0) {
        if (out_size) out[0] = '\0';
        return;
    }
    marker += strlen(name);
    while (*marker == ' ') ++marker;
    length = 0;
    while (marker[length] && marker[length] != '\r' && marker[length] != '\n' && marker[length] != ';') ++length;
    if (length >= out_size) length = out_size - 1;
    memcpy(out, marker, length);
    out[length] = '\0';
}

static int request_once(
    const char *host,
    unsigned short port,
    const char *request,
    char *response,
    size_t response_size,
    const char *step,
    char *status,
    size_t status_size)
{
    int sock = connect_rtsp(host, port);
    int result;
    if (sock < 0) {
        snprintf(status, status_size, "RTSP %s CONNECT", step);
        return -1;
    }
    result = exchange(sock, request, response, response_size);
    sceNetSocketClose(sock);
    if (result == kReset || result == -kReset) {
        snprintf(status, status_size, "RTSP %s RESET", step);
        vita_debug_log("[GameStream] %s", status);
        return -1;
    }
    if (result <= 0 || status_code(response) != 200) {
        if (response[0]) snprintf(status, status_size, "RTSP %s %d", step, status_code(response));
        else snprintf(status, status_size, "RTSP %s NO REPLY %d", step, result);
        vita_debug_log("[GameStream] %s", status);
        return -1;
    }
    return 0;
}

static int setup_stream(
    const char *host,
    unsigned short port,
    const char *session,
    const char *stream,
    int rtp_port,
    int seq,
    char *response,
    size_t response_size,
    const char *step,
    char *status,
    size_t status_size)
{
    char request[1024];
    snprintf(request, sizeof(request),
             "SETUP rtsp://%s:%u/streamid=%s RTSP/1.0\r\n"
             "CSeq: %d\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n"
             "%s%s%s"
             "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d;mode=play\r\n"
             "\r\n",
             host, port, stream, seq, host,
             session[0] ? "Session: " : "",
             session,
             session[0] ? "\r\n" : "",
             rtp_port, rtp_port + 1);
    return request_once(host, port, request, response, response_size, step, status, status_size);
}

}

extern "C" int moonlight_rtsp_start(const char *session_url, char *status, size_t status_size)
{
    char host[128];
    unsigned short port = 0;
    char request[3072];
    char response[8192];
    char session[64];
    char sdp[2048];
    int video_sock = -1;
    int audio_sock = -1;
    int control_sock = -1;
    int packets = 0;
    int video_port = 0;
    int sdp_length;

    if (!status || status_size == 0) return -1;
    snprintf(status, status_size, "RTSP FAIL");
    session[0] = '\0';
    if (parse_rtsp_url(session_url, host, sizeof(host), &port) != 0) {
        snprintf(status, status_size, "RTSP BAD URL");
        return -1;
    }

    snprintf(request, sizeof(request),
             "OPTIONS rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 1\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n"
             "\r\n",
             host, port, host);
    if (request_once(host, port, request, response, sizeof(response), "OPTIONS", status, status_size) != 0) return -1;

    snprintf(request, sizeof(request),
             "DESCRIBE rtsp://%s:%u RTSP/1.0\r\n"
             "CSeq: 2\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n"
             "Accept: application/sdp\r\n"
             "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
             "\r\n",
             host, port, host);
    if (request_once(host, port, request, response, sizeof(response), "DESCRIBE", status, status_size) != 0) return -1;

    video_sock = bind_udp(kVideoPort);
    audio_sock = bind_udp(kAudioPort);
    if (video_sock < 0) {
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        snprintf(status, status_size, "RTP BIND %d", video_sock);
        return -1;
    }

    if (setup_stream(host, port, session, "audio/0/0", kAudioPort, 3, response, sizeof(response), "AUDIO", status, status_size) != 0) {
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        return -1;
    }
    copy_token(response, "Session:", session, sizeof(session));
    if (setup_stream(host, port, session, "video/0/0", kVideoPort, 4, response, sizeof(response), "VIDEO", status, status_size) != 0) {
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        return -1;
    }
    video_port = server_port(response);
    if (!session[0]) copy_token(response, "Session:", session, sizeof(session));
    send_ping(video_sock, host, video_port ? (unsigned short)video_port : kVideoPort);
    send_ping(audio_sock, host, kAudioPort);
    if (setup_stream(host, port, session, "control/13/0", 47995, 5, response, sizeof(response), "CONTROL", status, status_size) != 0) {
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        return -1;
    }

    if (start_enet_control(host, kControlPort, &control_sock) != 0) {
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        snprintf(status, status_size, "ENET CONTROL");
        return -1;
    }

    sdp_length = snprintf(sdp, sizeof(sdp),
                          "v=0\r\n"
                          "o=android 0 14 IN IP4 127.0.0.1\r\n"
                          "s=NVIDIA Streaming Client\r\n"
                          "t=0 0\r\n"
                          "m=video 47998 RTP/AVP 96\r\n"
                          "a=rtpmap:96 H264/90000\r\n"
                          "a=x-nv-video[0].clientViewportWd:1280\r\n"
                          "a=x-nv-video[0].clientViewportHt:720\r\n"
                          "a=x-nv-video[0].maxFPS:60\r\n"
                          "a=x-nv-video[0].packetSize:1024\r\n"
                          "a=x-nv-video[0].rateControlMode:4\r\n"
                          "a=x-nv-video[0].timeoutLengthMs:7000\r\n"
                          "a=x-nv-video[0].framesWithInvalidRefThreshold:0\r\n"
                          "a=x-nv-video[0].initialBitrateKbps:4000\r\n"
                          "a=x-nv-video[0].initialPeakBitrateKbps:4000\r\n"
                          "a=x-nv-video[0].videoEncoderSlicesPerFrame:1\r\n"
                          "a=x-nv-video[0].maxNumReferenceFrames:1\r\n"
                          "a=x-nv-video[0].encoderCscMode:0\r\n"
                          "a=x-nv-video[0].dynamicRangeMode:0\r\n"
                          "a=x-nv-vqos[0].bw.minimumBitrateKbps:4000\r\n"
                          "a=x-nv-vqos[0].bw.maximumBitrateKbps:20000\r\n"
                          "a=x-nv-vqos[0].fec.enable:1\r\n"
                          "a=x-nv-vqos[0].fec.minRequiredFecPackets:0\r\n"
                          "a=x-nv-vqos[0].bitStreamFormat:0\r\n"
                          "a=x-nv-vqos[0].qosTrafficType:5\r\n"
                          "a=x-nv-vqos[0].videoQualityScoreUpdateTime:5000\r\n"
                          "a=x-nv-aqos.packetDuration:5\r\n"
                          "a=x-nv-aqos.qosTrafficType:4\r\n"
                          "a=x-nv-audio.surround.numChannels:2\r\n"
                          "a=x-nv-audio.surround.channelMask:3\r\n"
                          "a=x-nv-audio.surround.AudioQuality:0\r\n"
                          "a=x-nv-general.useReliableUdp:0\r\n"
                          "a=x-nv-general.featureFlags:135\r\n"
                          "a=x-ss-general.encryptionEnabled:0\r\n"
                          "a=x-ss-video[0].chromaSamplingType:0\r\n"
                          "a=x-ss-video[0].intraRefresh:0\r\n"
                          "a=x-ml-general.featureFlags:0\r\n"
                          "a=x-ml-video.configuredBitrateKbps:20000\r\n");
    if (sdp_length < 0 || sdp_length >= (int)sizeof(sdp)) {
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        snprintf(status, status_size, "RTSP ANNOUNCE SDP");
        return -1;
    }
    snprintf(request, sizeof(request),
             "ANNOUNCE rtsp://%s:%u/streamid=video RTSP/1.0\r\n"
             "CSeq: 6\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n"
             "%s%s%s"
             "Content-Type: application/sdp\r\n"
             "Content-Length: %d\r\n"
             "\r\n"
             "%s",
             host, port, host,
             session[0] ? "Session: " : "",
             session,
             session[0] ? "\r\n" : "",
             sdp_length, sdp);
    if (request_once(host, port, request, response, sizeof(response), "ANNOUNCE", status, status_size) != 0) {
        if (control_sock >= 0) sceNetSocketClose(control_sock);
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        return -1;
    }

    snprintf(request, sizeof(request),
             "PLAY rtsp://%s:%u/ RTSP/1.0\r\n"
             "CSeq: 7\r\n"
             "X-GS-ClientVersion: 14\r\n"
             "Host: %s\r\n"
             "%s%s%s"
             "\r\n",
             host, port, host,
             session[0] ? "Session: " : "",
             session,
             session[0] ? "\r\n" : "");
    if (request_once(host, port, request, response, sizeof(response), "PLAY", status, status_size) != 0) {
        if (control_sock >= 0) sceNetSocketClose(control_sock);
        sceNetSocketClose(video_sock);
        if (audio_sock >= 0) sceNetSocketClose(audio_sock);
        return -1;
    }

    packets = count_packets(video_sock, audio_sock, host, (unsigned short)video_port, 6000);
    if (control_sock >= 0) sceNetSocketClose(control_sock);
    sceNetSocketClose(video_sock);
    if (audio_sock >= 0) sceNetSocketClose(audio_sock);
    snprintf(status, status_size, "RTP %d v%d", packets, video_port);
    vita_debug_log("[GameStream] %s session %s", status, session[0] ? session : "none");
    return 0;
}
