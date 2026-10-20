#ifndef VITA_MOONLIGHT_APPLICATION_SERVICE_H
#define VITA_MOONLIGHT_APPLICATION_SERVICE_H

#include "backend/moonlight_backend.h"

class ConnectionService;

class ApplicationService {
public:
    ApplicationService(MoonlightBackend &backend, ConnectionService &connection);

    int GetAll(MoonlightApplication *out, int capacity);
    int Refresh();

private:
    MoonlightBackend &m_backend;
    ConnectionService &m_connection;
};

#endif
