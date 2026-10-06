#ifndef VITA_MOONLIGHT_NOTIFICATION_SERVICE_H
#define VITA_MOONLIGHT_NOTIFICATION_SERVICE_H

#include <psp2/notificationutil.h>

class NotificationService {
public:
    NotificationService();

    int Initialize();
    void Shutdown();

    int StartConnecting(const char *address);
    void FinishConnection(bool success, const char *address);
    bool IsProgressActive() const { return m_progress_active; }

private:
    bool m_initialized;
    bool m_progress_active;
};

#endif
