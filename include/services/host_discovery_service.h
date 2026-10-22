#ifndef VITA_MOONLIGHT_HOST_DISCOVERY_SERVICE_H
#define VITA_MOONLIGHT_HOST_DISCOVERY_SERVICE_H

#include "backend/moonlight_backend.h"

class HostDiscoveryService {
public:
    explicit HostDiscoveryService(MoonlightBackend &backend);

    /* Main-thread lifecycle calls. Start returns the backend's actual start status;
     * the backend owns the scan worker and natural-completion state. */
    int Start();
    int Stop();
    int GetHosts(MoonlightHost *out, int capacity);

private:
    MoonlightBackend &m_backend;
};

#endif
