#ifndef VITA_MOONLIGHT_CONNECTION_SERVICE_H
#define VITA_MOONLIGHT_CONNECTION_SERVICE_H

#include "backend/moonlight_backend.h"

class ConnectionService {
public:
    explicit ConnectionService(MoonlightBackend &backend);
    ~ConnectionService();


    int Connect(const MoonlightHost &host);
    void FinishConnection(bool success, const char *address);
    bool IsSuccessEventReady() const { return m_success_event_ready; }
    void ConsumeSuccessEvent();
    void Shutdown();
    int Start(int application_id);
    int Stop();
    int Disconnect();

    MoonlightConnectionState State() const;

private:
    static void DialogPollTask(void *userdata);

    void StartConnectionDialog();
    void ShowConnectionError(const char *address);
    void TryShowConnectionError();

    MoonlightBackend &m_backend;
    bool m_progress_dialog_open;
    bool m_progress_dialog_closing;
    bool m_error_dialog_open;
    bool m_error_pending;
    bool m_dialog_task_registered;
    bool m_success_event_pending;
    bool m_success_event_ready;
    char m_error_message[512];
};

#endif
