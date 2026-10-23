#include <string.h>
#include <psp2/net/net.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "legacy_host_probe.h"

int legacy_host_probe(const MoonlightHost *host, const MoonlightCancellation *cancel)
{
    SceNetSockaddrIn address;
    SceNetEpollEvent event;
    int socket_id = -1, epoll_id = -1, result = 0;
    int nonblocking = 1, socket_error = 0;
    unsigned int error_size = sizeof(socket_error);
    uint64_t deadline;

    if (!host || !host->internal[0]) return -1;
    if (moonlight_is_cancelled(cancel)) return -2;
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = SCE_NET_AF_INET;
    address.sin_port = sceNetHtons(host->port ? host->port : 47989);
    if (sceNetInetPton(SCE_NET_AF_INET, host->internal, &address.sin_addr) != 1) {
        int resolver = sceNetResolverCreate("MoonlightProbe", NULL, 0);
        if (resolver < 0) return -1;
        /* Native bounded DNS on this worker; cancellation is checked on return. */
        int resolved = sceNetResolverStartNtoa(resolver, host->internal,
                                              &address.sin_addr, 1, 1, 0);
        sceNetResolverDestroy(resolver);
        if (moonlight_is_cancelled(cancel)) return -2;
        if (resolved < 0) return 0;
    }

    socket_id = sceNetSocket("MoonlightProbe", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (socket_id < 0) return -1;
    if (sceNetSetsockopt(socket_id, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO,
                         &nonblocking, sizeof(nonblocking)) < 0) {
        result = -1;
        goto done;
    }
    if (sceNetConnect(socket_id, (SceNetSockaddr *)&address, sizeof(address)) == 0) {
        result = 1;
        goto done;
    }
    if (*sceNetErrnoLoc() != SCE_NET_EINPROGRESS) goto done;
    epoll_id = sceNetEpollCreate("MoonlightProbe", 0);
    if (epoll_id < 0) { result = -1; goto done; }
    memset(&event, 0, sizeof(event));
    event.events = SCE_NET_EPOLLOUT | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
    if (sceNetEpollControl(epoll_id, SCE_NET_EPOLL_CTL_ADD, socket_id, &event) < 0) {
        result = -1;
        goto done;
    }
    deadline = sceKernelGetProcessTimeWide() + 1000000;
    while (!moonlight_is_cancelled(cancel) && sceKernelGetProcessTimeWide() < deadline) {
        int ready = sceNetEpollWait(epoll_id, &event, 1, 0);
        if (ready < 0) { result = -1; break; }
        if (ready > 0) {
            if ((event.events & SCE_NET_EPOLLOUT) &&
                !(event.events & (SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP))) {
                if (sceNetGetsockopt(socket_id, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR,
                                     &socket_error, &error_size) < 0) result = -1;
                else result = socket_error == 0 ? 1 : 0;
            }
            break;
        }
        sceKernelDelayThread(10000);
    }
done:
    if (epoll_id >= 0) sceNetEpollDestroy(epoll_id);
    sceNetSocketClose(socket_id);
    return moonlight_is_cancelled(cancel) ? -2 : result;
}
