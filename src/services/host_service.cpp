#include "services/host_service.h"

HostService::HostService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int HostService::GetHosts(MoonlightHost *out, int capacity)
{
    return m_backend.GetHosts(out, capacity);
}

int HostService::Add(const char *address, uint16_t port, const char *name)
{
    return m_backend.AddHost(address, port, name);
}
