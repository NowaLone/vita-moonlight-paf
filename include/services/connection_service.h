#ifndef VITA_MOONLIGHT_CONNECTION_SERVICE_H
#define VITA_MOONLIGHT_CONNECTION_SERVICE_H

#include "backend/moonlight_backend.h"

class ConnectionService {
public:
    explicit ConnectionService(MoonlightBackend &backend);
    ~ConnectionService();


    int Connect(const MoonlightHost &host);
    void FinishConnection(bool success, const char *address);
    void Shutdown();
    int Start(int application_id);
    int Stop();
    int Disconnect();

    MoonlightConnectionState State() const;

private:
    static void DialogPollTask(void *userdata);

    void StartConnectionDialog();
    void CloseDialog();
    void ShowConnectionError(const char *address);

    MoonlightBackend &m_backend;
    bool m_progress_dialog_open;
    bool m_error_dialog_open;
    bool m_dialog_task_registered;
    char m_error_message[512];
};

#endif
