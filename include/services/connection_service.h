#ifndef VITA_MOONLIGHT_CONNECTION_SERVICE_H
#define VITA_MOONLIGHT_CONNECTION_SERVICE_H

#include "backend/moonlight_backend.h"

class ConnectionService {
public:
    explicit ConnectionService(MoonlightBackend &backend);

    int Start(const char *address);
    int Stop();

private:
    MoonlightBackend &m_backend;
};

#endif
