#include <paf.h>

#include "services/application_service.h"
#include "services/connection_service.h"

namespace {

class RefreshAppsJob : public paf::job::JobItem {
public:
    RefreshAppsJob(MoonlightBackend &backend, ConnectionService &connection)
        : paf::job::JobItem("ApplicationService::RefreshAppsJob", NULL),
          m_backend(backend), m_connection(connection)
    {
    }

    virtual ~RefreshAppsJob() {}

    virtual void Run()
    {
        int result = m_backend.GetApplications(NULL, 0);
        m_connection.RecordWorkerResult(result);
    }

    virtual void Finish() {}

private:
    MoonlightBackend &m_backend;
    ConnectionService &m_connection;
};

}

ApplicationService::ApplicationService(MoonlightBackend &backend, ConnectionService &connection)
    : m_backend(backend), m_connection(connection)
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

    int result = m_connection.BeginOperation(ConnectionService::Operation_Applications);
    if (result != 0) return result;

    paf::common::SharedPtr<paf::job::JobItem> item(new RefreshAppsJob(m_backend, m_connection));
    result = paf::job::JobQueue::default_queue->Enqueue(item);
    if (result != 0) m_connection.FinishUnstartedOperation();
    return result;
}
