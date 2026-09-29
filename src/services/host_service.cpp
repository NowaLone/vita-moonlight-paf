#include <paf.h>

#include "services/host_service.h"

namespace {

class SearchJob : public paf::job::JobItem {
public:
    explicit SearchJob(MoonlightBackend &backend)
        : paf::job::JobItem("HostService::SearchJob", NULL),
          m_backend(backend)
    {
    }

    virtual ~SearchJob()
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

HostService::HostService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int HostService::GetHosts(MoonlightHost *out, int capacity)
{
    return m_backend.GetHosts(out, capacity);
}

int HostService::Search()
{
    if (paf::job::JobQueue::default_queue == NULL) {
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(
        new SearchJob(m_backend)
    );

    return paf::job::JobQueue::default_queue->Enqueue(item);
}

int HostService::Add(const char *address, uint16_t port, const char *name)
{
    return m_backend.AddHost(address, port, name);
}
