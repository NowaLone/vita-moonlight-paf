/*
 * mDNS UDP Sniffer for PSVita (VitaSDK)
 * Detection of Moonlight/Sunshine (GameStream) services on the local network.
 *
 * Author: AorsiniYT
 * Copyright 2025
 */

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "udp_sniffer_vita.h"

#define BUF_SIZE 2048
#define MCAST_ADDR "224.0.0.251"
#define MCAST_PORT 5353
#define MAX_SUNSHINE 16
#define HOSTNAME_LEN 256

/* Return encoded bytes consumed, or -1 for malformed/truncated/overlong names.
 * Bound pointer traversal by packet length so compression cycles cannot hang discovery. */
static int decode_dns_name(const unsigned char* msg, int msglen, int offset, char* out, int outlen) {
    int consumed = 0, jumped = 0, pos = offset, outpos = 0, steps = 0;
    if (!msg || !out || outlen <= 0 || offset < 0 || offset >= msglen) return -1;
    out[0] = '\0';
    while (pos < msglen && steps < msglen) {
        unsigned char c = msg[pos];
        ++steps;
        if (c == 0) return jumped ? consumed : consumed + 1;
        if ((c & 0xC0) == 0xC0) {
            if (pos + 1 >= msglen) return -1;
            int ptr = ((c & 0x3F) << 8) | msg[pos + 1];
            if (ptr >= msglen) return -1;
            if (!jumped) consumed += 2;
            pos = ptr;
            jumped = 1;
        } else {
            if ((c & 0xC0) != 0 || c > msglen - pos - 1) return -1;
            int separator = outpos != 0;
            if (c + separator >= outlen - outpos) return -1;
            if (separator) {
                out[outpos] = '.';
                ++outpos;
            }
            memcpy(out + outpos, msg + pos + 1, c);
            outpos += c;
            out[outpos] = '\0';
            if (!jumped) consumed += c + 1;
            pos += c + 1;
        }
    }
    return -1;
}

struct sunshine_entry {
    char host[HOSTNAME_LEN];
    int port;
    unsigned int last_seen;
    char ip[16];
    char target[HOSTNAME_LEN];
};

static struct sunshine_entry sunshine_table[MAX_SUNSHINE];
static int sock = -1;
static int initialized = 0;
static int moonlight_count = 0;

static void update_sunshine(const char* host, int port, const char* target) {
    unsigned int now = sceKernelGetProcessTimeLow() / 1000;
    int idx = -1;
    for (int i = 0; i < MAX_SUNSHINE; ++i) {
        if (strcmp(sunshine_table[i].host, host) == 0) { idx = i; break; }
    }
    if (idx == -1) {
        unsigned int oldest = now;
        for (int i = 0; i < MAX_SUNSHINE; ++i) {
            if (sunshine_table[i].host[0] == '\0') { idx = i; break; }
            if (sunshine_table[i].last_seen < oldest) { oldest = sunshine_table[i].last_seen; idx = i; }
        }
        if (idx >= 0) {
            strncpy(sunshine_table[idx].host, host, HOSTNAME_LEN);
            sunshine_table[idx].port = 0;
            sunshine_table[idx].ip[0] = '\0';
            sunshine_table[idx].target[0] = '\0';
        }
    }
    if (idx >= 0) {
        if (port > 0) sunshine_table[idx].port = port;
        if (target && target[0]) {
            strncpy(sunshine_table[idx].target, target, sizeof(sunshine_table[idx].target) - 1);
            sunshine_table[idx].target[sizeof(sunshine_table[idx].target) - 1] = '\0';
        }
        sunshine_table[idx].last_seen = now;
    }
}

static void update_sunshine_ip_by_target(const char* target, const char* ip) {
    unsigned int now = sceKernelGetProcessTimeLow() / 1000;
    for (int i = 0; i < MAX_SUNSHINE; ++i) {
        if (strcmp(sunshine_table[i].target, target) == 0 && sunshine_table[i].host[0]) {
            strncpy(sunshine_table[i].ip, ip, sizeof(sunshine_table[i].ip));
            sunshine_table[i].last_seen = now;
            break;
        }
    }
}

static moonlight_found_cb g_found_cb = NULL;

void udp_sniffer_vita_set_callback(moonlight_found_cb cb) {
    g_found_cb = cb;
}

static void check_and_print_ready_entries_vita() {
    for (int i = 0; i < MAX_SUNSHINE; ++i) {
        if (sunshine_table[i].host[0] && sunshine_table[i].target[0] && sunshine_table[i].ip[0] && sunshine_table[i].port > 0) {
            moonlight_count++;

            if (g_found_cb) {

                g_found_cb(moonlight_count, sunshine_table[i].host, sunshine_table[i].target, sunshine_table[i].ip, sunshine_table[i].port);
            }
            sunshine_table[i].host[0] = '\0';
            sunshine_table[i].ip[0] = '\0';
            sunshine_table[i].port = 0;
            sunshine_table[i].target[0] = '\0';
        }
    }
}

static int open_socket(void) {
    if (!initialized) {

        sock = sceNetSocket("mdns", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);

        if (sock < 0) return sock;
        SceNetSockaddrIn addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = SCE_NET_AF_INET;
        addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
        addr.sin_port = sceNetHtons(MCAST_PORT);
        int bindres = sceNetBind(sock, (SceNetSockaddr*)&addr, sizeof(addr));

        if (bindres < 0) { sceNetSocketClose(sock); sock = -1; return bindres; }
        struct SceNetIpMreq mreq;
        // Reemplazo de sceNetInetAddr por sceNetInetPton
        int ptonres = sceNetInetPton(SCE_NET_AF_INET, MCAST_ADDR, &mreq.imr_multiaddr.s_addr);

        (void)ptonres;
        mreq.imr_interface.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
        int sockoptres = sceNetSetsockopt(sock, SCE_NET_IPPROTO_IP, SCE_NET_IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

        if (sockoptres < 0) { sceNetSocketClose(sock); sock = -1; return sockoptres; }
        initialized = 1;
    }
    return 0;
}

int udp_sniffer_vita_query(void) {
    int result = open_socket();
    if (result < 0) return result;
    unsigned char query[] = {
        0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
        9,'_','n','v','s','t','r','e','a','m',
        4,'_','t','c','p',
        5,'l','o','c','a','l',0x00,
        0x00,0x0C,0x00,0x01
    };
    SceNetSockaddrIn mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = SCE_NET_AF_INET;
    int ptonres2 = sceNetInetPton(SCE_NET_AF_INET, MCAST_ADDR, &mcast_addr.sin_addr.s_addr);

    (void)ptonres2;
    mcast_addr.sin_port = sceNetHtons(MCAST_PORT);
    int sendto_res = sceNetSendto(sock, query, sizeof(query), SCE_NET_MSG_DONTWAIT, (SceNetSockaddr*)&mcast_addr, sizeof(mcast_addr));

    if (sendto_res == sizeof(query)) return 0;
    return sendto_res < 0 ? sendto_res : -1;
}

int udp_sniffer_vita_poll(void) {
    if (!initialized || sock < 0) return -1;
    char buffer[BUF_SIZE];
    SceNetSockaddrIn from;
    unsigned int fromlen = sizeof(from);
    int n = sceNetRecvfrom(sock, buffer, sizeof(buffer), SCE_NET_MSG_DONTWAIT,
                          (SceNetSockaddr*)&from, &fromlen);
    if (n < 0) {
        int error = sceNetErrnoLoc() ? *sceNetErrnoLoc() : 0;
        return error == SCE_NET_EAGAIN ? 0 : n;
    }
    if (n == 0) return 0;
    const unsigned char* pkt = (const unsigned char*)buffer;
    int msglen = n;
    int qdcount = 0, ancount = 0;
    if (msglen > 12) {
        qdcount = (pkt[4]<<8) | pkt[5];
        ancount = (pkt[6]<<8) | pkt[7];
        int nscount = (pkt[8]<<8) | pkt[9];
        int arcount = (pkt[10]<<8) | pkt[11];
        int pos = 12;
        for (int q = 0; q < qdcount && pos < msglen; ++q) {
            char qname[256];
            int l = decode_dns_name(pkt, msglen, pos, qname, sizeof(qname));
            if (l < 0 || l > msglen - pos - 4) return 0;
            pos += l + 4;
        }
        int total_rr = ancount + nscount + arcount;
        for (int i = 0; i < total_rr && pos < msglen; ++i) {
            char rrname[256];
            int l = decode_dns_name(pkt, msglen, pos, rrname, sizeof(rrname));
            if (l < 0 || l > msglen - pos - 10) return 0;
            pos += l;
            int type = (pkt[pos]<<8) | pkt[pos+1];
            int rdlength = (pkt[pos+8]<<8) | pkt[pos+9];
            pos += 10;
            if (rdlength > msglen - pos) return 0;
            if (type == 12) { // PTR
                char ptrname[256];
                int name_length = decode_dns_name(pkt, msglen, pos, ptrname, sizeof(ptrname));
                if (name_length < 0 || name_length > rdlength) return 0;
                if (strstr(ptrname, "_nvstream._tcp.local") != NULL) {
                    update_sunshine(ptrname, 0, NULL);
                }
            }
            if (type == 33 && rdlength >= 6) { // SRV
                int srv_port = (pkt[pos+4]<<8) | pkt[pos+5];
                char target[256] = {0};
                int name_length = decode_dns_name(pkt, msglen, pos+6, target, sizeof(target));
                if (name_length < 0 || name_length > rdlength - 6) return 0;
                size_t len = strlen(rrname);
                const char* suffix = "._nvstream._tcp.local";
                size_t suffixlen = strlen(suffix);
                if (len > suffixlen && strcmp(rrname + len - suffixlen, suffix) == 0) {
                    if (target[0]) {
                        update_sunshine(rrname, srv_port, target);
                    }
                }
            }
            if (type == 1 && rdlength == 4) { // A
                char ip[16];
                snprintf(ip, sizeof(ip), "%u.%u.%u.%u", pkt[pos], pkt[pos+1], pkt[pos+2], pkt[pos+3]);
                update_sunshine_ip_by_target(rrname, ip);
            }
            pos += rdlength;
        }
        check_and_print_ready_entries_vita();
    }
    return 0;
}

void udp_sniffer_vita_deinit(void) {
    if (sock >= 0) {
        sceNetSocketClose(sock);
        sock = -1;
    }
    initialized = 0;
    moonlight_count = 0;
    memset(sunshine_table, 0, sizeof(sunshine_table));
}

void udp_sniffer_vita_init(void) {
    // Does nothing, lazy initialization in poll
    initialized = 0;
    moonlight_count = 0;
    memset(sunshine_table, 0, sizeof(sunshine_table));
}
#else
int udp_sniffer_vita_poll(void) { return -1; }
int udp_sniffer_vita_query(void) { return -1; }
#endif
