#ifndef VITA_MOONLIGHT_CONNECTION_SERVICE_H
#define VITA_MOONLIGHT_CONNECTION_SERVICE_H

#include "backend/moonlight_backend.h"

class PairingService;

class ConnectionService {
public:
    ConnectionService(MoonlightBackend &backend, PairingService &pairing);
    ~ConnectionService();

    int Connect(const MoonlightHost &host);
    enum ForegroundOperation {
        Operation_None,
        Operation_Connect,
        Operation_Applications,
        Operation_Launch
    };

    /* PAF main thread only: one foreground action owns the native dialog. */
    int BeginOperation(ForegroundOperation operation);
    void FinishUnstartedOperation();
    bool PrepareCompletion(const MoonlightEvent &event);
    bool IsBusy() const;
    void ShowError(const char *message, int result = 0);
    /* Workers publish only a result; PAF consumes it after Run's backend call returns. */
    void RecordWorkerResult(int result);
    bool GetCompletedJobEvent(MoonlightEvent *event) const;

    void Shutdown();
    int Start(int application_id);
    int Stop();
    int Disconnect();

    MoonlightConnectionState State() const;
    int GetStreamStats(MoonlightStreamStats *out);
    int GetControllerLight(MoonlightControllerLight *out);

private:
    static void DialogPollTask(void *userdata);

    int StartWaitDialog();
    void CloseWaitDialog();
    void ShowConnectionError();
    void TryShowError();

    MoonlightBackend &m_backend;
    PairingService &m_pairing;
    int32_t m_progress_dialog_slot;
    bool m_error_dialog_open;
    bool m_error_pending;
    bool m_dialog_task_registered;
    ForegroundOperation m_operation;
    int m_worker_done;
    int m_worker_result;
    char m_connect_address[256];
    char m_error_message[512];
};

#endif
