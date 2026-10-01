#include <string.h>

#include <paf.h>

#include "services/connection_service.h"

namespace {

class ConnectJob : public paf::job::JobItem {
public:
    ConnectJob(MoonlightBackend &backend, const MoonlightHost &host)
        : paf::job::JobItem("ConnectionService::ConnectJob", NULL),
          m_backend(backend),
          m_host(host)
    {
    }

    virtual ~ConnectJob() {}

    virtual void Run()
    {
        m_backend.ConnectHost(m_host);
    }

    virtual void Finish() {}

private:
    MoonlightBackend &m_backend;
    MoonlightHost m_host;
};

}

ConnectionService::ConnectionService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int ConnectionService::Connect(const MoonlightHost &host)
{
    if (!host.internal[0]) {
        return -1;
    }

    if (paf::job::JobQueue::default_queue == NULL) {
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(
        new ConnectJob(m_backend, host)
    );

    return paf::job::JobQueue::default_queue->Enqueue(item);
}

int ConnectionService::Start(int application_id)
{
    return m_backend.StartApplication(application_id);
}

int ConnectionService::Stop()
{
    return m_backend.StopApplication();
}

int ConnectionService::Disconnect()
{
    return m_backend.DisconnectHost();
}

MoonlightConnectionState ConnectionService::State() const
{
    return m_backend.GetConnectionState();
}
