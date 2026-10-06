#ifndef VITA_MOONLIGHT_CONNECTION_SERVICE_H
#define VITA_MOONLIGHT_CONNECTION_SERVICE_H

#include "backend/moonlight_backend.h"
#include "services/notification_service.h"

class ConnectionService {
public:
    ConnectionService(MoonlightBackend &backend, NotificationService &notifications);

    int Connect(const MoonlightHost &host);
    int Start(int application_id);
    int Stop();
    int Disconnect();

    MoonlightConnectionState State() const;

private:
    MoonlightBackend &m_backend;
    NotificationService &m_notifications;
};

#endif
