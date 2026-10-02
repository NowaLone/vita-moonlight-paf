#ifndef VITA_MOONLIGHT_APPLICATION_SERVICE_H
#define VITA_MOONLIGHT_APPLICATION_SERVICE_H

#include "backend/moonlight_backend.h"

class ApplicationService {
public:
    explicit ApplicationService(MoonlightBackend &backend);

    int GetAll(MoonlightApplication *out, int capacity);
    int Refresh();

private:
    MoonlightBackend &m_backend;
};

#endif
