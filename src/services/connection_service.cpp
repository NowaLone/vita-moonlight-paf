#include <string.h>
#include <stdio.h>

#include <paf.h>
#include "platform/paf_common_gui_dialog.h"

#include <psp2/message_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>

#include "services/connection_service.h"
#include "common.h"
#include "services/pairing_service.h"
#include "debug.h"

namespace {

static bool completes_operation(ConnectionService::ForegroundOperation operation,
                                MoonlightEventType type)
{
    switch (operation) {
    case ConnectionService::Operation_Connect:
        return type == MOONLIGHT_EVENT_CONNECTION_READY ||
               type == MOONLIGHT_EVENT_CONNECTION_FAILED;
    case ConnectionService::Operation_Applications:
        return type == MOONLIGHT_EVENT_APPLICATIONS_READY ||
               type == MOONLIGHT_EVENT_APPLICATIONS_FAILED;
    case ConnectionService::Operation_Launch:
        return type == MOONLIGHT_EVENT_STREAM_STARTED ||
               type == MOONLIGHT_EVENT_STREAM_FAILED ||
               type == MOONLIGHT_EVENT_STREAM_STOPPED;
    default:
        return false;
    }
}

class ConnectJob : public paf::job::JobItem {
public:
    ConnectJob(MoonlightBackend &backend, const MoonlightHost &host, ConnectionService &connection)
        : paf::job::JobItem("ConnectionService::ConnectJob", NULL),
          m_backend(backend),
          m_host(host),
          m_connection(connection)
    {
    }

    virtual ~ConnectJob()
    {
        vita_debug_log("[ConnectionService] job=%p destroyed", (void *)this);
    }

    virtual void Run()
    {
        vita_debug_log(
            "[ConnectionService] ConnectJob::Run host=%s job=%p",
            m_host.internal, (void *)this);
        int result = m_backend.ConnectHost(m_host);
        vita_debug_log(
            "[ConnectionService] ConnectJob::Run result=%d job=%p",
            result, (void *)this);
        m_connection.RecordWorkerResult(result);
    }

    virtual void Finish()
    {
        vita_debug_log("[ConnectionService] job=%p Finish", (void *)this);
    }

private:
    MoonlightBackend &m_backend;
    MoonlightHost m_host;
    ConnectionService &m_connection;
};

}

ConnectionService::ConnectionService(MoonlightBackend &backend, PairingService &pairing)
    : m_backend(backend),
      m_pairing(pairing),
      m_progress_dialog_slot(-1),
      m_progress_close_requested(false),
      m_progress_polled(false),
      m_progress_started_us(0),
      m_progress_last_error(0),
      m_error_dialog_open(false),
      m_error_pending(false),
      m_dialog_task_registered(false),
      m_operation(Operation_None),
      m_release_on_dialog_close(false),
      m_worker_done(0),
      m_worker_result(0)
{
    m_error_message[0] = '\0';
    m_connect_address[0] = '\0';
}

ConnectionService::~ConnectionService()
{
    Shutdown();
}

bool ConnectionService::IsBusy() const
{
    return m_operation != Operation_None || m_progress_dialog_slot >= 0 ||
           m_error_dialog_open || m_error_pending;
}

int ConnectionService::BeginOperation(ForegroundOperation operation)
{
    if (operation == Operation_None || IsBusy()) return -1;

    m_operation = operation;
    m_release_on_dialog_close = false;
    __atomic_store_n(&m_worker_done, 0, __ATOMIC_RELEASE);
    m_worker_result = 0;
    int result = StartWaitDialog();
    if (result < 0) {
        m_operation = Operation_None;
        return result;
    }
    vita_debug_log("[Workflow] begin operation=%d", (int)operation);
    return 0;
}

int ConnectionService::StartWaitDialog()
{
    if (!g_plugin) {
        vita_debug_log("[Workflow] PAF busy dialog unavailable: no application plugin");
        return -1;
    }
    const char *message;
    switch (m_operation) {
    case Operation_Connect:
        message = "Connecting to PC...";
        break;
    case Operation_Applications:
        message = "Loading applications...";
        break;
    case Operation_Launch:
        message = "Starting stream...";
        break;
    default:
        return -1;
    }

    m_progress_started_us = sceKernelGetProcessTimeWide();
    /* Match BHBB/NetStream: CommonGuiDialog stays in the application's PAF context.
     * Keep this system module loaded for the same lifetime as the PAF framework. */
    int result = sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_COMMON_GUI_DIALOG);
    if (result != SCE_SYSMODULE_LOADED) {
        result = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_COMMON_GUI_DIALOG);
        if (result < 0) {
            vita_debug_log("[Workflow] PAF dialog module load failed result=0x%08X", (unsigned int)result);
            return result;
        }
    }

    paf::wstring title;
    paf::wstring text = paf::common::string_util::ToWString(message);
    int32_t slot = sce::CommonGuiDialog::Dialog::Show(
        g_plugin, &title, &text, &sce::CommonGuiDialog::Param::s_dialogTextSmallBusy,
        NULL, NULL);
    if (slot < 0) {
        vita_debug_log("[Workflow] PAF busy dialog open failed operation=%d result=0x%08X",
            (int)m_operation, (unsigned int)slot);
        return slot;
    }

    m_progress_dialog_slot = slot;
    m_progress_close_requested = false;
    m_progress_polled = false;
    m_progress_last_error = 0;
    vita_debug_log("[Workflow] PAF busy dialog open operation=%d slot=%d elapsed_ms=%llu",
        (int)m_operation, (int)slot,
        (unsigned long long)((sceKernelGetProcessTimeWide() - m_progress_started_us) / 1000));
    if (!m_dialog_task_registered) {
        paf::common::MainThreadCallList::Register(DialogPollTask, this);
        m_dialog_task_registered = true;
    }
    return 0;
}

void ConnectionService::CloseWaitDialog()
{
    if (m_progress_dialog_slot < 0) return;

    const bool first_request = !m_progress_close_requested;
    m_progress_close_requested = true;
    const int32_t slot = m_progress_dialog_slot;
    int result = sce::CommonGuiDialog::Dialog::Close(slot);
    if (first_request || result >= 0 || result != m_progress_last_error) {
        vita_debug_log("[Workflow] PAF busy dialog close operation=%d slot=%d result=0x%08X elapsed_ms=%llu",
            (int)m_operation, (int)slot, (unsigned int)result,
            (unsigned long long)((sceKernelGetProcessTimeWide() - m_progress_started_us) / 1000));
    }
    m_progress_last_error = result;
    if (result < 0) return; /* Keep ownership; the PAF poll retries without blocking. */

    /* BHBB releases its logical slot after Close. PAF owns widget/animation cleanup;
     * this API has no SceMsgDialog FINISHED/Term lifecycle to poll. */
    m_progress_dialog_slot = -1;
    m_progress_close_requested = false;
    if (m_release_on_dialog_close) {
        m_operation = Operation_None;
        m_release_on_dialog_close = false;
    }
}

void ConnectionService::ShowConnectionError()
{
    ShowError("Unable to connect to PC. Check the address, network connection and Sunshine.");
}

void ConnectionService::ShowError(const char *message, int result)
{
    if (!message) return;
    vita_debug_log("[Dialog] error: %s result=0x%08X", message, (unsigned int)result);
    /* Keep the displayed message immutable; repeated notifications are log-only. */
    if (m_error_pending || m_error_dialog_open) return;
    if (result != 0) {
        snprintf(m_error_message, sizeof(m_error_message), "%s (0x%08X)",
                 message, (unsigned int)result);
    } else {
        strncpy(m_error_message, message, sizeof(m_error_message) - 1);
    }
    m_error_message[sizeof(m_error_message) - 1] = '\0';
    m_error_pending = true;
    if (!m_dialog_task_registered) {
        paf::common::MainThreadCallList::Register(DialogPollTask, this);
        m_dialog_task_registered = true;
    }
}

void ConnectionService::TryShowError()
{
    if (!m_error_pending || m_progress_dialog_slot >= 0 || m_error_dialog_open) {
        return;
    }

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
        if (result != SCE_COMMON_DIALOG_ERROR_BUSY) {
            vita_debug_log(
                "[Dialog] error dialog init failed: 0x%08X",
                (unsigned int)result);
            m_error_pending = false;
        }
        return;
    }

    m_error_pending = false;
    m_error_dialog_open = true;
}

void ConnectionService::FinishUnstartedOperation()
{
    /* Enqueue/start failed synchronously: no worker completion event will arrive. */
    ForegroundOperation operation = m_operation;
    m_release_on_dialog_close = true;
    CloseWaitDialog();
    if (operation == Operation_Connect) ShowConnectionError();
}

bool ConnectionService::PrepareCompletion(const MoonlightEvent &event)
{
    if (!completes_operation(m_operation, event.type)) return true;
    if (m_operation != Operation_Launch &&
        !__atomic_load_n(&m_worker_done, __ATOMIC_ACQUIRE)) return false;

    CloseWaitDialog();
    if (m_progress_dialog_slot >= 0) return false;

    ForegroundOperation operation = m_operation;
    m_operation = Operation_None;
    vita_debug_log("[Workflow] complete operation=%d event=%d result=%d",
        (int)operation, (int)event.type, event.result);

    if (operation == Operation_Connect && event.type == MOONLIGHT_EVENT_CONNECTION_FAILED) {
        ShowConnectionError();
    }
    /* Release before dispatch: success may immediately start the next step. */
    return true;
}

void ConnectionService::RecordWorkerResult(int result)
{
    m_worker_result = result;
    __atomic_store_n(&m_worker_done, 1, __ATOMIC_RELEASE);
}

bool ConnectionService::GetCompletedJobEvent(MoonlightEvent *event) const
{
    if (!event || (m_operation != Operation_Connect && m_operation != Operation_Applications) ||
        !__atomic_load_n(&m_worker_done, __ATOMIC_ACQUIRE)) return false;

    memset(event, 0, sizeof(*event));
    event->host_id = -1;
    event->application_id = -1;
    event->result = m_worker_result;
    if (m_operation == Operation_Connect) {
        event->type = m_worker_result == 0
            ? MOONLIGHT_EVENT_CONNECTION_READY : MOONLIGHT_EVENT_CONNECTION_FAILED;
        event->address = m_connect_address;
    } else {
        event->type = m_worker_result >= 0
            ? MOONLIGHT_EVENT_APPLICATIONS_READY : MOONLIGHT_EVENT_APPLICATIONS_FAILED;
    }
    return true;
}

void ConnectionService::DialogPollTask(void *userdata)
{
    ConnectionService *service = (ConnectionService *)userdata;
    if (!service) {
        return;
    }

    if (service->m_progress_dialog_slot >= 0 && !service->m_progress_polled) {
        service->m_progress_polled = true;
        vita_debug_log("[Workflow] PAF busy dialog first poll operation=%d slot=%d elapsed_ms=%llu",
            (int)service->m_operation, (int)service->m_progress_dialog_slot,
            (unsigned long long)((sceKernelGetProcessTimeWide() - service->m_progress_started_us) / 1000));
    }
    if (service->m_progress_close_requested) {
        service->CloseWaitDialog();
    }

    if (service->m_error_pending && service->m_progress_dialog_slot < 0 &&
        !service->m_error_dialog_open) {
        service->TryShowError();
    }

    if (service->m_error_dialog_open) {
        if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            return;
        }

        SceMsgDialogResult result;
        memset(&result, 0, sizeof(result));
        sceMsgDialogGetResult(&result);
        sceMsgDialogTerm();

        service->m_error_dialog_open = false;
        service->m_error_message[0] = '\0';
    }

    if (service->m_progress_dialog_slot < 0 &&
        !service->m_error_pending &&
        !service->m_error_dialog_open) {
        if (service->m_dialog_task_registered) {
            paf::common::MainThreadCallList::Unregister(DialogPollTask, service);
            service->m_dialog_task_registered = false;
        }
    }
}

void ConnectionService::Shutdown()
{
    CloseWaitDialog();

    if (m_error_dialog_open) {
        sceMsgDialogAbort();
        if (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
            sceMsgDialogTerm();
        }
        m_error_dialog_open = false;
    }

    m_error_pending = false;
    m_progress_close_requested = false;
    m_operation = Operation_None;
    m_release_on_dialog_close = false;
    m_error_message[0] = '\0';

    if (m_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(DialogPollTask, this);
        m_dialog_task_registered = false;
    }
}

int ConnectionService::Connect(const MoonlightHost &host)
{
    if (!host.internal[0]) {
        ShowConnectionError();
        return -1;
    }

    int begin_result = BeginOperation(Operation_Connect);
    if (begin_result != 0) {
        if (!IsBusy()) ShowError("Unable to open the progress dialog.", begin_result);
        return begin_result;
    }
    strncpy(m_connect_address, host.internal, sizeof(m_connect_address) - 1);
    m_connect_address[sizeof(m_connect_address) - 1] = '\0';

    /* A previous page must not keep the shared queue occupied by an abandoned PIN wait. */
    m_pairing.Cancel();

    if (paf::job::JobQueue::default_queue == NULL) {
        vita_debug_log("[ConnectionService] Connect: no default job queue");
        FinishUnstartedOperation();
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(
        new ConnectJob(m_backend, host, *this)
    );

    int result = paf::job::JobQueue::default_queue->Enqueue(item);
    vita_debug_log(
        "[ConnectionService] Connect enqueue host=%s result=%d",
        host.internal,
        result);

    if (result != 0) {
        FinishUnstartedOperation();
    }

    return result;
}

int ConnectionService::Start(int application_id)
{
    vita_debug_log(
        "[ConnectionService] Start app=%d",
        application_id);

    int begin_result = BeginOperation(Operation_Launch);
    if (begin_result != 0) return begin_result;

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

    if (result != 0) FinishUnstartedOperation();
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

int ConnectionService::GetStreamStats(MoonlightStreamStats *out)
{
    return m_backend.GetStreamStats(out);
}
