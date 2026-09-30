#ifndef VITA_MOONLIGHT_HOST_SERVICE_H
#define VITA_MOONLIGHT_HOST_SERVICE_H

#include <stdint.h>
#include "backend/moonlight_backend.h"

class HostService {
public:
    explicit HostService(MoonlightBackend &backend);

    int GetHosts(MoonlightHost *out, int capacity);
    int Add(const char *address, uint16_t port, const char *name);

private:
    MoonlightBackend &m_backend;
};

#endif
