#ifndef VITA_MOONLIGHT_CONNECTION_SERVICE_H
#define VITA_MOONLIGHT_CONNECTION_SERVICE_H

#include "backend/moonlight_backend.h"

class ConnectionService {
public:
    explicit ConnectionService(MoonlightBackend &backend);

    int Connect(const MoonlightHost &host);
    int Start(int application_id);
    int Stop();
    int Disconnect();

    MoonlightConnectionState State() const;

private:
    MoonlightBackend &m_backend;
};

#endif
