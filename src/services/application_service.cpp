#include <paf.h>

#include "services/application_service.h"

namespace {

class RefreshAppsJob : public paf::job::JobItem {
public:
    explicit RefreshAppsJob(MoonlightBackend &backend)
        : paf::job::JobItem("ApplicationService::RefreshAppsJob", NULL),
          m_backend(backend)
    {
    }

    virtual ~RefreshAppsJob() {}

    virtual void Run()
    {
        m_backend.GetApplications(NULL, 0);
    }

    virtual void Finish() {}

private:
    MoonlightBackend &m_backend;
};

}

ApplicationService::ApplicationService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int ApplicationService::GetAll(MoonlightApplication *out, int capacity)
{
    return m_backend.GetApplications(out, capacity);
}

int ApplicationService::Refresh()
{
    if (paf::job::JobQueue::default_queue == NULL) {
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(new RefreshAppsJob(m_backend));
    return paf::job::JobQueue::default_queue->Enqueue(item);
}
