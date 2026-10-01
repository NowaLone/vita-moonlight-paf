#ifndef VITA_MOONLIGHT_HOST_DISCOVERY_SERVICE_H
#define VITA_MOONLIGHT_HOST_DISCOVERY_SERVICE_H

#include "backend/moonlight_backend.h"

class HostDiscoveryService {
public:
    explicit HostDiscoveryService(MoonlightBackend &backend);

    int Start();
    int Stop();
    int GetHosts(MoonlightHost *out, int capacity);

private:
    MoonlightBackend &m_backend;
    bool m_running;
};

#endif
