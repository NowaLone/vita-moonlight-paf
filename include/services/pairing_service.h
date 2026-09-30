#ifndef VITA_MOONLIGHT_PAIRING_SERVICE_H
#define VITA_MOONLIGHT_PAIRING_SERVICE_H

#include "backend/moonlight_backend.h"

class PairingService {
public:
    explicit PairingService(MoonlightBackend &backend);

    int Pair();

private:
    MoonlightBackend &m_backend;
};

#endif
