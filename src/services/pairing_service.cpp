#include "services/pairing_service.h"

PairingService::PairingService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int PairingService::Pair()
{
    return m_backend.PairCurrentHost();
}
