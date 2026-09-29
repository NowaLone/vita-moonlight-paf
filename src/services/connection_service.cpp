#include "services/connection_service.h"

ConnectionService::ConnectionService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int ConnectionService::Start(const char *address)
{
    return m_backend.StartStream(address);
}

int ConnectionService::Stop()
{
    return m_backend.StopStream();
}
