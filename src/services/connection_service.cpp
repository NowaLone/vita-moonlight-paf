#include <string.h>
#include <stdio.h>

#include <paf.h>
#include "platform/paf_common_gui_dialog.h"

#include <psp2/message_dialog.h>
#include <psp2/sysmodule.h>

#include "services/connection_service.h"
#include "common.h"
#include "services/pairing_service.h"
#include "services/host_service.h"
#include "debug.h"
#ifdef MOONLIGHT_DISCONNECT_TEST
#include "app/disconnect_test.h"
#endif

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
    ConnectJob(MoonlightBackend &backend, const MoonlightHost &host, ConnectionService &connection, HostService &hosts)
        : paf::job::JobItem("ConnectionService::ConnectJob", NULL),
          m_backend(backend),
          m_host(host),
          m_connection(connection),
          m_probe_barrier(hosts.AvailabilityBarrier())
    {
    }

    virtual ~ConnectJob() {}

    virtual void Run()
    {
        m_backend.WaitForHostSearchIdle();
        HostService::WaitForAvailability(m_probe_barrier);
        int result = m_backend.ConnectHost(m_host);
        if (result != 0) {
            vita_debug_log("[ConnectionService] Connect job failed result=%d", result);
        }
        m_connection.RecordWorkerResult(result);
    }

    virtual void Finish() {}

private:
    MoonlightBackend &m_backend;
    MoonlightHost m_host;
    ConnectionService &m_connection;
    paf::common::SharedPtr<paf::job::JobItem> m_probe_barrier;
};

}

ConnectionService::ConnectionService(MoonlightBackend &backend, PairingService &pairing, HostService &hosts)
    : m_backend(backend),
      m_pairing(pairing),
      m_hosts(hosts),
      m_progress_dialog_slot(-1),
      m_error_dialog_open(false),
      m_error_pending(false),
      m_confirm_application_id(0),
      m_confirm_running_id(0),
      m_dialog_task_registered(false),
      m_operation(Operation_None),
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

    m_backend.StopHostSearch();
    m_hosts.CancelAvailability();
    m_operation = operation;
    __atomic_store_n(&m_worker_done, 0, __ATOMIC_RELEASE);
    m_worker_result = 0;
    int result = StartWaitDialog();
    if (result < 0) {
        m_operation = Operation_None;
        return result;
    }
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
    if (!m_dialog_task_registered) {
        paf::common::MainThreadCallList::Register(DialogPollTask, this);
        m_dialog_task_registered = true;
    }
    return 0;
}

void ConnectionService::CloseWaitDialog()
{
    if (m_progress_dialog_slot < 0) return;

    const int32_t slot = m_progress_dialog_slot;
    /* The inspected native Close leaves internal values (including the stack guard)
     * in r0, not a status code. Match BHBB/NetStream: call once, discard the return
     * and release our slot. PAF owns the remaining widget/animation cleanup. */
    (void)sce::CommonGuiDialog::Dialog::Close(slot);
    m_progress_dialog_slot = -1;
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

void ConnectionService::ConfirmQuitAndStart(int application_id, int running_application_id)
{
    if (application_id <= 0 || running_application_id <= 0 ||
        application_id == running_application_id || IsBusy()) {
        ShowError("Another application is running on the PC. Close it and try again.");
        return;
    }
    ShowError("Another application is running on the PC. Quit it and start the selected application? "
              "Any unsaved progress will be lost. This also ends other streaming sessions.");
    m_confirm_application_id = application_id;
    m_confirm_running_id = running_application_id;
    vita_debug_log("[Workflow] remote quit confirmation selected=%d active=%d",
        application_id, running_application_id);
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
    user_message.buttonType = m_confirm_application_id > 0
        ? SCE_MSG_DIALOG_BUTTON_TYPE_YESNO : SCE_MSG_DIALOG_BUTTON_TYPE_OK;
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
            m_confirm_application_id = 0;
            m_confirm_running_id = 0;
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
    CloseWaitDialog();
    m_operation = Operation_None;
    if (operation == Operation_Connect) ShowConnectionError();
}

bool ConnectionService::PrepareCompletion(const MoonlightEvent &event)
{
    if (!completes_operation(m_operation, event.type)) return true;
    if (m_operation != Operation_Launch &&
        !__atomic_load_n(&m_worker_done, __ATOMIC_ACQUIRE)) return false;

    CloseWaitDialog();

    ForegroundOperation operation = m_operation;
    m_operation = Operation_None;

    if (operation == Operation_Connect && event.type == MOONLIGHT_EVENT_CONNECTION_FAILED) {
        ShowConnectionError();
    }
    if (operation == Operation_Launch && event.type == MOONLIGHT_EVENT_STREAM_FAILED) {
        if (event.result == MOONLIGHT_STREAM_APP_CONFLICT &&
            event.running_application_id > 0 && event.running_application_id != event.application_id) {
            ConfirmQuitAndStart(event.application_id, event.running_application_id);
        } else if (event.result == MOONLIGHT_STREAM_QUIT_FAILED) {
            ShowError("Unable to quit the running application. Close it on the PC or "
                      "from the device that started it, then try again.", event.result);
        } else {
            ShowError("Unable to start stream.", event.result);
        }
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
        int get_result = sceMsgDialogGetResult(&result);
        if (get_result < 0) {
            vita_debug_log("[Dialog] unable to read dialog result=0x%08X", (unsigned int)get_result);
        }
        if (sceMsgDialogTerm() < 0) return;

        service->m_error_dialog_open = false;
        service->m_error_message[0] = '\0';
        const int application_id = service->m_confirm_application_id;
        const int running_id = service->m_confirm_running_id;
        service->m_confirm_application_id = 0;
        service->m_confirm_running_id = 0;
        if (get_result >= 0 && application_id > 0 &&
            result.result == SCE_COMMON_DIALOG_RESULT_OK &&
            result.buttonId == SCE_MSG_DIALOG_BUTTON_ID_YES) {
            int start_result = service->StartConfirmed(application_id, running_id);
            if (start_result != 0) {
                service->ShowError("Unable to start stream.", start_result);
            }
        }
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
    m_confirm_application_id = 0;
    m_confirm_running_id = 0;
    m_operation = Operation_None;
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
#ifdef MOONLIGHT_DISCONNECT_TEST
    disconnect_test_capture_host(host);
#endif
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
        new ConnectJob(m_backend, host, *this, m_hosts)
    );

    int result = paf::job::JobQueue::default_queue->Enqueue(item);
    if (result != 0) {
        vita_debug_log("[ConnectionService] Connect enqueue failed result=%d", result);
        FinishUnstartedOperation();
    }

    return result;
}

int ConnectionService::Start(int application_id)
{
    return StartConfirmed(application_id, 0);
}

int ConnectionService::StartConfirmed(int application_id, int quit_application_id)
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
    int result = m_backend.StartApplication(application_id, quit_application_id);
#if defined(MOONLIGHT_PENDING_DISCONNECT_TEST) || defined(MOONLIGHT_STARTUP_DISCONNECT_TEST)
    disconnect_test_capture_start(application_id, result);
#endif

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

int ConnectionService::GetControllerLight(MoonlightControllerLight *out)
{
    return m_backend.GetControllerLight(out);
}
