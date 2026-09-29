#include "services/pairing_service.h"

PairingService::PairingService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int PairingService::Pair(const char *address)
{
    return m_backend.PairHost(address);
}
