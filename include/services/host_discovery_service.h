#ifndef VITA_MOONLIGHT_HOST_DISCOVERY_SERVICE_H
#define VITA_MOONLIGHT_HOST_DISCOVERY_SERVICE_H

#include "backend/moonlight_backend.h"

class HostDiscoveryService {
public:
    explicit HostDiscoveryService(MoonlightBackend &backend);

    /* Main-thread activation renews the backend lease; no network work or join here. */
    int Start();
    int Stop();
    bool IsIdle();
    bool Poll(MoonlightEvent *event);
    bool Accepts(const MoonlightEvent &event) const;
    int GetHosts(MoonlightHost *out, int capacity);

private:
    MoonlightBackend &m_backend;
    uint32_t m_generation;
    MoonlightDiscoverySnapshot m_snapshot;
};

#endif
