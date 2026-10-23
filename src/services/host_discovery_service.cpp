#include <string.h>
#include "services/host_discovery_service.h"

HostDiscoveryService::HostDiscoveryService(MoonlightBackend &backend)
    : m_backend(backend), m_generation(0)
{
    memset(&m_snapshot, 0, sizeof(m_snapshot));
}

int HostDiscoveryService::Start()
{
    return m_backend.SearchHosts(&m_generation);
}

int HostDiscoveryService::Stop()
{
    m_generation = 0; /* Invalidate already queued publications immediately. */
    return m_backend.StopHostSearch();
}

bool HostDiscoveryService::IsIdle()
{
    return m_backend.IsHostSearchIdle();
}

bool HostDiscoveryService::Poll(MoonlightEvent *event)
{
    if (!m_generation || m_backend.ReadDiscovery(&m_snapshot) != 1 ||
        m_snapshot.generation != m_generation) return false;
    memset(event, 0, sizeof(*event));
    event->type = MOONLIGHT_EVENT_DISCOVERY_CHANGED;
    event->discovery_generation = m_generation;
    return true;
}

bool HostDiscoveryService::Accepts(const MoonlightEvent &event) const
{
    return m_generation != 0 && event.discovery_generation == m_generation;
}

int HostDiscoveryService::GetHosts(MoonlightHost *out, int capacity)
{
    int count = m_snapshot.count;
    if (out && capacity > 0) {
        int copied = count < capacity ? count : capacity;
        memcpy(out, m_snapshot.hosts, copied * sizeof(MoonlightHost));
    }
    return count;
}
