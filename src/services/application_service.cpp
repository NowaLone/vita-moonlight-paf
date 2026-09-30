#include "services/application_service.h"

ApplicationService::ApplicationService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

int ApplicationService::GetAll(MoonlightApplication *out, int capacity)
{
    return m_backend.GetApplications(out, capacity);
}
