#include <string.h>

#include <paf.h>

#include <psp2/message_dialog.h>

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
    : m_backend(backend),
      m_progress_dialog_open(false),
      m_error_dialog_open(false),
      m_dialog_task_registered(false)
{
    m_error_message[0] = '\0';
}

ConnectionService::~ConnectionService()
{
    Shutdown();
}

void ConnectionService::StartConnectionDialog()
{
    if (m_progress_dialog_open || m_error_dialog_open) {
        return;
    }

    SceMsgDialogParam param;
    SceMsgDialogSystemMessageParam system_message;
    memset(&param, 0, sizeof(param));
    memset(&system_message, 0, sizeof(system_message));

    sceMsgDialogParamInit(&param);
    system_message.sysMsgType = SCE_MSG_DIALOG_SYSMSG_TYPE_WAIT;
    param.mode = SCE_MSG_DIALOG_MODE_SYSTEM_MSG;
    param.sysMsgParam = &system_message;

    int result = sceMsgDialogInit(&param);
    if (result < 0) {
        vita_debug_log(
            "[ConnectionService] connection dialog init failed: 0x%08X",
            (unsigned int)result);
        return;
    }

    m_progress_dialog_open = true;
}

void ConnectionService::CloseDialog()
{
    if (m_progress_dialog_open || m_error_dialog_open) {
        sceMsgDialogAbort();
        sceMsgDialogTerm();
    }

    m_progress_dialog_open = false;
    m_error_dialog_open = false;
    m_error_message[0] = '\0';

    if (m_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(DialogPollTask, this);
        m_dialog_task_registered = false;
    }
}

void ConnectionService::ShowConnectionError(const char *address)
{
    (void)address;

    CloseDialog();

    strncpy(
        m_error_message,
        "Unable to connect to PC. Check the address, network connection and Sunshine.",
        sizeof(m_error_message) - 1);
    m_error_message[sizeof(m_error_message) - 1] = '\0';

    SceMsgDialogParam param;
    SceMsgDialogUserMessageParam user_message;
    memset(&param, 0, sizeof(param));
    memset(&user_message, 0, sizeof(user_message));

    sceMsgDialogParamInit(&param);
    user_message.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    user_message.msg = (const SceChar8 *)m_error_message;
    user_message.buttonParam = NULL;
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user_message;

    int result = sceMsgDialogInit(&param);
    if (result < 0) {
        vita_debug_log(
            "[ConnectionService] connection error dialog init failed: 0x%08X",
            (unsigned int)result);
        m_error_message[0] = '\0';
        return;
    }

    m_error_dialog_open = true;
    if (!m_dialog_task_registered) {
        paf::common::MainThreadCallList::Register(DialogPollTask, this);
        m_dialog_task_registered = true;
    }
}

void ConnectionService::FinishConnection(bool success, const char *address)
{
    if (success) {
        CloseDialog();
        return;
    }

    ShowConnectionError(address);
}

void ConnectionService::DialogPollTask(void *userdata)
{
    ConnectionService *service = (ConnectionService *)userdata;
    if (!service || !service->m_error_dialog_open) {
        if (service && service->m_dialog_task_registered) {
            paf::common::MainThreadCallList::Unregister(DialogPollTask, service);
            service->m_dialog_task_registered = false;
        }
        return;
    }

    if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return;
    }

    SceMsgDialogResult result;
    memset(&result, 0, sizeof(result));
    sceMsgDialogGetResult(&result);
    sceMsgDialogTerm();

    service->m_error_dialog_open = false;
    service->m_error_message[0] = '\0';

    if (service->m_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(DialogPollTask, service);
        service->m_dialog_task_registered = false;
    }
}

void ConnectionService::Shutdown()
{
    CloseDialog();
}

int ConnectionService::Connect(const MoonlightHost &host)
{
    if (!host.internal[0]) {
        return -1;
    }

    StartConnectionDialog();

    if (paf::job::JobQueue::default_queue == NULL) {
        vita_debug_log("[ConnectionService] Connect: no default job queue");
        FinishConnection(false, host.internal);
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

    if (result != 0) {
        FinishConnection(false, host.internal);
    }

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

