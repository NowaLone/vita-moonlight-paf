#include <string.h>

#include <paf.h>

#include "services/connection_service.h"
#include "debug.h"

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
        vita_debug_log(
            "[ConnectionService] ConnectJob::Run host=%s",
            m_host.internal);
        int result = m_backend.ConnectHost(m_host);
        vita_debug_log(
            "[ConnectionService] ConnectJob::Run result=%d",
            result);
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
        vita_debug_log("[ConnectionService] Connect: no default job queue");
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(
        new ConnectJob(m_backend, host)
    );

    int result = paf::job::JobQueue::default_queue->Enqueue(item);
    vita_debug_log(
        "[ConnectionService] Connect enqueue host=%s result=%d",
        host.internal,
        result);
    return result;
}

int ConnectionService::Start(int application_id)
{
    vita_debug_log(
        "[ConnectionService] Start app=%d",
        application_id);

    /*
     * moonlight_stream_start() creates its own worker thread, so putting the
     * operation into PAF's JobQueue only adds another asynchronous hop and
     * can hide launch failures from the caller/UI.
     */
    int result = m_backend.StartApplication(application_id);

    vita_debug_log(
        "[ConnectionService] Start app=%d result=%d",
        application_id,
        result);

    return result;
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
