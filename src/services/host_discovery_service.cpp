#include <paf.h>

#include "services/host_discovery_service.h"

namespace {

class DiscoveryJob : public paf::job::JobItem {
public:
    explicit DiscoveryJob(MoonlightBackend &backend)
        : paf::job::JobItem("HostDiscoveryService::DiscoveryJob", NULL),
          m_backend(backend)
    {
    }

    virtual ~DiscoveryJob()
    {
    }

    virtual void Run()
    {
        m_backend.SearchHosts();
    }

    virtual void Finish()
    {
    }

private:
    MoonlightBackend &m_backend;
};

}

HostDiscoveryService::HostDiscoveryService(MoonlightBackend &backend)
    : m_backend(backend),
      m_running(false)
{
}

int HostDiscoveryService::Start()
{
    if (m_running) {
        return 0;
    }

    if (paf::job::JobQueue::default_queue == NULL) {
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(
        new DiscoveryJob(m_backend)
    );

    int result = paf::job::JobQueue::default_queue->Enqueue(item);
    if (result == 0) {
        m_running = true;
    }
    return result;
}

int HostDiscoveryService::Stop()
{
    /*
     * The backend currently owns the legacy discovery worker. Cancellation
     * will be delegated there once the legacy discovery implementation is
     * attached. Until then this only releases the service-side state.
     */
    m_running = false;
    return 0;
}
