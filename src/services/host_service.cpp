#include <paf.h>

#include "services/host_service.h"

namespace {

class SearchJob : public paf::job::JobItem {
public:
    SearchJob()
        : paf::job::JobItem("HostService::SearchJob", NULL)
    {
    }

    virtual ~SearchJob()
    {
    }

    virtual void Run()
    {
        moonlight_api_search_hosts();
    }

    virtual void Finish()
    {
    }
};

}

int HostService::GetHosts(MoonlightHost *out, int capacity)
{
    return moonlight_api_get_hosts(out, capacity);
}

int HostService::Search()
{
    if (paf::job::JobQueue::default_queue == NULL) {
        return -1;
    }

    paf::common::SharedPtr<paf::job::JobItem> item(
        new SearchJob()
    );

    return paf::job::JobQueue::default_queue->Enqueue(item);
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
