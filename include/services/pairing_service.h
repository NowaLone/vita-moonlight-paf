#ifndef VITA_MOONLIGHT_PAIRING_SERVICE_H
#define VITA_MOONLIGHT_PAIRING_SERVICE_H

#include "backend/moonlight_backend.h"

class PairingService {
public:
    explicit PairingService(MoonlightBackend &backend);

    int Prepare(char out_pin[5]);
    int Pair(const char pin[5]);

private:
    MoonlightBackend &m_backend;
};

#endif
