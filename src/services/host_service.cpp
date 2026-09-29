#include "services/host_service.h"

int HostService::GetHosts(MoonlightHost *out, int capacity)
{
    return moonlight_api_get_hosts(out, capacity);
}

int HostService::Search()
{
    return moonlight_api_search_hosts();
}

int HostService::Add(const char *address, uint16_t port, const char *name)
{
    return moonlight_api_add_host(address, port, name);
}

int HostService::Pair(const char *address)
{
    return moonlight_api_pair_host(address);
}

int HostService::StartStream(const char *address)
{
    return moonlight_api_start_stream(address);
}

int HostService::StopStream()
{
    return moonlight_api_stop_stream();
}
