#include <psp2/kernel/threadmgr.h>
#include <string.h>

#include "legacy_host_discovery.h"
#include "mdns/udp_sniffer_vita.h"

#define LEGACY_HOST_DISCOVERY_MAX_HOSTS 16
#define LEGACY_HOST_DISCOVERY_DURATION_TICKS 100
#define LEGACY_HOST_DISCOVERY_TICK_USEC 100000

enum {
    DISCOVERY_IDLE = 0,
    DISCOVERY_RUNNING = 1,
    DISCOVERY_STOP_REQUESTED = 2
};

static volatile int s_status = DISCOVERY_IDLE;
static SceUID s_thread = -1;
static LegacyHostDiscoveryCallback s_callback = NULL;
static void *s_userdata = NULL;

static MoonlightHost s_hosts[LEGACY_HOST_DISCOVERY_MAX_HOSTS];
static int s_host_count = 0;
static int s_next_id = 0;

static void emit_event(LegacyHostDiscoveryEventType type, const MoonlightHost *host)
{
    LegacyHostDiscoveryCallback callback = s_callback;
    if (callback != NULL) {
        callback(type, host, s_userdata);
    }
}

static void clear_hosts(void)
{
    memset(s_hosts, 0, sizeof(s_hosts));
    s_host_count = 0;
    s_next_id = 0;
}

static int find_host(const char *name, const char *address, uint16_t port)
{
    int i;
    for (i = 0; i < s_host_count; ++i) {
        if (strcmp(s_hosts[i].name, name) == 0 &&
            strcmp(s_hosts[i].internal, address) == 0 &&
            s_hosts[i].port == port) {
            return i;
        }
    }
    return -1;
}

static void on_mdns_host(
    int idx,
    const char *host,
    const char *pcname,
    const char *ip,
    int port)
{
    (void)idx;
    (void)host;

    if (pcname == NULL || ip == NULL || pcname[0] == '\0' || ip[0] == '\0' || port <= 0) {
        return;
    }

    if (find_host(pcname, ip, (uint16_t)port) >= 0) {
        return;
    }

    if (s_host_count >= LEGACY_HOST_DISCOVERY_MAX_HOSTS) {
        return;
    }

    MoonlightHost *entry = &s_hosts[s_host_count];
    memset(entry, 0, sizeof(*entry));

    entry->id = s_next_id++;
    strncpy(entry->name, pcname, sizeof(entry->name) - 1);
    strncpy(entry->internal, ip, sizeof(entry->internal) - 1);
    entry->port = (uint16_t)port;
    entry->paired = 0;
    entry->online = 1;
    entry->prefer_external = 0;

    ++s_host_count;
    emit_event(LEGACY_HOST_DISCOVERY_FOUND, entry);
}

static int discovery_thread(SceSize args, void *argp)
{
    int ticks = 0;
    (void)args;
    (void)argp;

    udp_sniffer_vita_deinit();
    udp_sniffer_vita_init();
    udp_sniffer_vita_set_callback(on_mdns_host);

    while (s_status == DISCOVERY_RUNNING &&
           ticks < LEGACY_HOST_DISCOVERY_DURATION_TICKS) {
        udp_sniffer_vita_poll();
        sceKernelDelayThread(LEGACY_HOST_DISCOVERY_TICK_USEC);
        ++ticks;
    }

    udp_sniffer_vita_set_callback(NULL);
    udp_sniffer_vita_deinit();

    if (s_status != DISCOVERY_IDLE) {
        s_status = DISCOVERY_IDLE;
        emit_event(LEGACY_HOST_DISCOVERY_FINISHED, NULL);
    }

    return 0;
}

int legacy_host_discovery_init(LegacyHostDiscoveryCallback callback, void *userdata)
{
    s_callback = callback;
    s_userdata = userdata;
    s_status = DISCOVERY_IDLE;
    s_thread = -1;
    clear_hosts();
    return 0;
}

void legacy_host_discovery_shutdown(void)
{
    legacy_host_discovery_stop();
    s_callback = NULL;
    s_userdata = NULL;
    clear_hosts();
}

int legacy_host_discovery_start(void)
{
    if (s_status == DISCOVERY_RUNNING) {
        return 0;
    }

    if (s_thread >= 0) {
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }

    clear_hosts();
    s_status = DISCOVERY_RUNNING;

    s_thread = sceKernelCreateThread(
        "mlmdns",
        discovery_thread,
        0x10000100,
        0x10000,
        0,
        0,
        NULL
    );

    if (s_thread < 0) {
        s_status = DISCOVERY_IDLE;
        s_thread = -1;
        return -1;
    }

    int result = sceKernelStartThread(s_thread, 0, NULL);
    if (result < 0) {
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
        s_status = DISCOVERY_IDLE;
        return result;
    }

    return 0;
}

int legacy_host_discovery_stop(void)
{
    if (s_status == DISCOVERY_RUNNING) {
        s_status = DISCOVERY_STOP_REQUESTED;
    }

    if (s_thread >= 0) {
        SceUInt timeout = LEGACY_HOST_DISCOVERY_TICK_USEC;
        int result;

        do {
            result = sceKernelWaitThreadEnd(s_thread, NULL, &timeout);
        } while (result < 0);

        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }

    if (s_status != DISCOVERY_IDLE) {
        s_status = DISCOVERY_IDLE;
        emit_event(LEGACY_HOST_DISCOVERY_FINISHED, NULL);
    }

    return 0;
}

int legacy_host_discovery_is_running(void)
{
    return s_status == DISCOVERY_RUNNING ||
           s_status == DISCOVERY_STOP_REQUESTED;
}

int legacy_host_discovery_get_hosts(MoonlightHost *out, int capacity)
{
    int count = s_host_count;
    int copy_count = count < capacity ? count : capacity;

    if (out != NULL && copy_count > 0) {
        memcpy(out, s_hosts, sizeof(MoonlightHost) * copy_count);
    }

    return count;
}
