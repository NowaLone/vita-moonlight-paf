#include <pthread.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "legacy_host_discovery.h"
#include "mdns/udp_sniffer_vita.h"
#include "debug.h"

#define DISCOVERY_QUERY_USEC 5000000ull
#define DISCOVERY_EXPIRY_USEC 15000000ull
#define DISCOVERY_LEASE_USEC 250000ull
#define DISCOVERY_POLL_USEC 10000
#define DISCOVERY_STACK_BYTES 65536

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_wake = PTHREAD_COND_INITIALIZER;
static pthread_t s_thread;
static int s_thread_created;
static int s_enabled;
static int s_shutdown;
static uint32_t s_generation;
static uint32_t s_active_generation;
static uint64_t s_lease_until;
static MoonlightDiscoverySnapshot s_snapshot;
/* Ages advance only during active, successful network polling, never across pause/error. */
static uint64_t s_healthy_time;
static uint64_t s_seen[MOONLIGHT_MAX_DISCOVERED_HOSTS];
static int s_last_error;

static void report_error(int result)
{
    if (result != s_last_error && result < 0)
        vita_debug_log("[Discovery] background search error=%d", result);
    /* Do not repeat the same outage every five seconds. Reset after recovery. */
    s_last_error = result;
}

static int generation_active(uint32_t generation, uint64_t now)
{
    return s_enabled && !s_shutdown && generation == s_generation && now < s_lease_until;
}

static void on_mdns_host(int idx, const char *service, const char *name,
                         const char *ip, int port)
{
    (void)idx;
    (void)service;
    if (!name || !ip || !name[0] || !ip[0] || port <= 0 || port > 65535) return;
    pthread_mutex_lock(&s_mutex);
    if (!generation_active(s_active_generation, sceKernelGetProcessTimeWide())) {
        pthread_mutex_unlock(&s_mutex);
        return;
    }
    int index;
    for (index = 0; index < s_snapshot.count; ++index) {
        if (s_snapshot.hosts[index].port == port &&
            strcmp(s_snapshot.hosts[index].internal, ip) == 0) break;
    }
    if (index < MOONLIGHT_MAX_DISCOVERED_HOSTS) {
        MoonlightHost *host = &s_snapshot.hosts[index];
        if (index == s_snapshot.count) {
            memset(host, 0, sizeof(*host));
            host->id = -1;
            strncpy(host->internal, ip, sizeof(host->internal) - 1);
            host->port = (uint16_t)port;
            ++s_snapshot.count;
            ++s_snapshot.revision;
        }
        if (strncmp(host->name, name, sizeof(host->name) - 1) != 0) {
            strncpy(host->name, name, sizeof(host->name) - 1);
            host->name[sizeof(host->name) - 1] = 0;
            ++s_snapshot.revision;
        }
        s_seen[index] = s_healthy_time;
    }
    pthread_mutex_unlock(&s_mutex);
}

static void expire_hosts(void)
{
    for (int i = s_snapshot.count - 1; i >= 0; --i) {
        if (s_healthy_time - s_seen[i] >= DISCOVERY_EXPIRY_USEC) {
            --s_snapshot.count;
            memmove(&s_snapshot.hosts[i], &s_snapshot.hosts[i + 1],
                    (s_snapshot.count - i) * sizeof(MoonlightHost));
            memmove(&s_seen[i], &s_seen[i + 1],
                    (s_snapshot.count - i) * sizeof(uint64_t));
            ++s_snapshot.revision;
        }
    }
}

static void *discovery_thread(void *argp)
{
    (void)argp;
    for (;;) {
        pthread_mutex_lock(&s_mutex);
        while (!s_shutdown && (!s_enabled ||
               sceKernelGetProcessTimeWide() >= s_lease_until))
            pthread_cond_wait(&s_wake, &s_mutex);
        if (s_shutdown) {
            pthread_mutex_unlock(&s_mutex);
            return NULL;
        }
        uint32_t generation = s_generation;
        s_active_generation = generation;
        pthread_mutex_unlock(&s_mutex);

        udp_sniffer_vita_init();
        udp_sniffer_vita_set_callback(on_mdns_host);
        uint64_t next_query = 0;
        uint64_t last_tick = sceKernelGetProcessTimeWide();
        int healthy = 0;
        for (;;) {
            uint64_t now = sceKernelGetProcessTimeWide();
            pthread_mutex_lock(&s_mutex);
            int active = generation_active(generation, now);
            pthread_mutex_unlock(&s_mutex);
            if (!active) break;
            if (now >= next_query) {
                int result = udp_sniffer_vita_query();
                healthy = result >= 0;
                if (result < 0) {
                    report_error(result);
                    udp_sniffer_vita_deinit();
                }
                /* Completion-to-next-query, no catch-up bursts after delay/suspend. */
                next_query = sceKernelGetProcessTimeWide() + DISCOVERY_QUERY_USEC;
                last_tick = sceKernelGetProcessTimeWide();
            }
            int result = healthy ? udp_sniffer_vita_poll() : -1;
            if (result < 0 && healthy) {
                healthy = 0;
                report_error(result);
                udp_sniffer_vita_deinit();
            }
            if (healthy) report_error(0);
            now = sceKernelGetProcessTimeWide();
            pthread_mutex_lock(&s_mutex);
            if (healthy && generation_active(generation, sceKernelGetProcessTimeWide())) {
                s_healthy_time += now >= last_tick ? now - last_tick : 0;
                expire_hosts();
            }
            pthread_mutex_unlock(&s_mutex);
            last_tick = now;
            sceKernelDelayThread(DISCOVERY_POLL_USEC);
        }
        udp_sniffer_vita_set_callback(NULL);
        udp_sniffer_vita_deinit();
        pthread_mutex_lock(&s_mutex);
        s_active_generation = 0;
        pthread_cond_broadcast(&s_wake);
        pthread_mutex_unlock(&s_mutex);
    }
}

int legacy_host_discovery_init(void)
{
    s_shutdown = 0;
    s_enabled = 0;
    s_generation = 0;
    s_active_generation = 0;
    s_healthy_time = 0;
    s_last_error = 0;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    return 0;
}

int legacy_host_discovery_start(uint32_t *generation)
{
    pthread_mutex_lock(&s_mutex);
    uint64_t now = sceKernelGetProcessTimeWide();
    if (s_shutdown) {
        pthread_mutex_unlock(&s_mutex);
        return -1;
    }
    if (!s_thread_created) {
        /* pthread_cond_wait needs pthread TLS; a raw kernel thread has none. */
        pthread_attr_t attr;
        int result = pthread_attr_init(&attr);
        if (result == 0) {
            result = pthread_attr_setstacksize(&attr, DISCOVERY_STACK_BYTES);
            if (result == 0) result = pthread_create(&s_thread, &attr, discovery_thread, NULL);
            pthread_attr_destroy(&attr);
        }
        if (result != 0) {
            pthread_mutex_unlock(&s_mutex);
            return -result; /* Keep background Start's negative-error contract. */
        }
        s_thread_created = 1;
    }
    if (!s_enabled || now >= s_lease_until) {
        if (++s_generation == 0) ++s_generation;
        s_snapshot.generation = s_generation;
        ++s_snapshot.revision;
        pthread_cond_broadcast(&s_wake);
    }
    s_enabled = 1;
    s_lease_until = now + DISCOVERY_LEASE_USEC;
    *generation = s_generation;
    pthread_mutex_unlock(&s_mutex);
    return 0;
}

int legacy_host_discovery_stop(void)
{
    pthread_mutex_lock(&s_mutex);
    s_enabled = 0;
    pthread_mutex_unlock(&s_mutex);
    return 0;
}

int legacy_host_discovery_is_idle(void)
{
    pthread_mutex_lock(&s_mutex);
    int idle = s_active_generation == 0;
    pthread_mutex_unlock(&s_mutex);
    return idle;
}

void legacy_host_discovery_wait_idle(void)
{
    /* PAF job threads need not have pthread TLS either. This worker-only barrier
     * follows HostService's existing native sleep/poll pattern, never the UI. */
    while (!legacy_host_discovery_is_idle()) sceKernelDelayThread(DISCOVERY_POLL_USEC);
}

int legacy_host_discovery_read(MoonlightDiscoverySnapshot *snapshot)
{
    pthread_mutex_lock(&s_mutex);
    int changed = -1;
    if (generation_active(s_generation, sceKernelGetProcessTimeWide())) {
        changed = snapshot->generation != s_snapshot.generation ||
                  snapshot->revision != s_snapshot.revision;
        if (changed) *snapshot = s_snapshot;
    }
    pthread_mutex_unlock(&s_mutex);
    return changed;
}

void legacy_host_discovery_shutdown(void)
{
    pthread_mutex_lock(&s_mutex);
    s_enabled = 0;
    s_shutdown = 1;
    pthread_cond_broadcast(&s_wake);
    pthread_mutex_unlock(&s_mutex);
    if (s_thread_created) {
        while (pthread_join(s_thread, NULL) != 0)
            sceKernelDelayThread(DISCOVERY_POLL_USEC);
        s_thread_created = 0;
    }
}
