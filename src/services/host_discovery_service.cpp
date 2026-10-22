#include "services/host_discovery_service.h"

HostDiscoveryService::HostDiscoveryService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int HostDiscoveryService::Start()
{
    /* SearchHosts only starts the backend's existing Vita worker. An extra PAF
     * job hides start errors and can run after Stop/the Search page has closed. */
    return m_backend.SearchHosts();
}

int HostDiscoveryService::Stop()
{
    /* Also reap a naturally completed worker; no second running flag to go stale. */
    return m_backend.StopHostSearch();
}

int HostDiscoveryService::GetHosts(MoonlightHost *out, int capacity)
{
    return m_backend.GetDiscoveredHosts(out, capacity);
}
