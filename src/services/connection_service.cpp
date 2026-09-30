#include "services/connection_service.h"

ConnectionService::ConnectionService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int ConnectionService::Connect(const MoonlightHost &host)
{
    return m_backend.ConnectHost(host);
}

int ConnectionService::Start(int application_id)
{
    return m_backend.StartApplication(application_id);
}

int ConnectionService::Stop()
{
    return m_backend.StopApplication();
}

int ConnectionService::Disconnect()
{
    return m_backend.DisconnectHost();
}

MoonlightConnectionState ConnectionService::State() const
{
    return m_backend.GetConnectionState();
}
