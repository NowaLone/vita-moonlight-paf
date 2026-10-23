#include "services/host_service.h"
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

HostService::HostService(MoonlightBackend &backend)
    : m_backend(backend), m_queue(NULL), m_owner(NULL), m_dirty(false)
{
}

int HostService::GetHosts(MoonlightHost *out, int capacity)
{
    return m_backend.GetHosts(out, capacity);
}

int HostService::Add(const char *address, uint16_t port, const char *name)
{
    return m_backend.AddHost(address, port, name);
}

int HostService::Delete(const MoonlightHost &host)
{
    return m_backend.DeleteHost(host);
}

/* A result belongs to both the server identity and its exact connection route. */
static bool same_probe_host(const MoonlightHost &a, const MoonlightHost &b)
{
    return strcmp(a.host_id, b.host_id) == 0 &&
           strcmp(a.internal, b.internal) == 0 && a.port == b.port;
}

class HostService::AvailabilityJob : public paf::job::JobItem {
public:
    struct Entry {
        MoonlightHost host;
        int result;
        uint64_t checked_at;
        uint64_t previous_checked_at;
        bool previous_available;
        bool displayed;
    };
    AvailabilityJob(MoonlightBackend &backend, const paf::vector<MoonlightHost> &hosts,
                    const AvailabilityJob *previous)
        : JobItem("PC availability", NULL), backend(backend), cancel(), done(0),
          started_at(sceKernelGetProcessTimeWide())
    {
        entries.resize(hosts.size());
        for (size_t i = 0; i < hosts.size(); ++i) {
            entries[i].host = hosts[i];
            entries[i].result = 0;
            entries[i].checked_at = 0;
            /* Copy only a completed, unchanged scan. These fallback fields stay immutable. */
            entries[i].previous_available = previous && previous->Available(previous->entries[i]);
            entries[i].previous_checked_at = entries[i].previous_available
                ? previous->entries[i].checked_at : 0;
            entries[i].displayed = entries[i].previous_available;
        }
    }
    virtual void Run()
    {
        for (size_t i = 0; i < entries.size() && !moonlight_is_cancelled(&cancel); ++i) {
            int result = backend.ProbeHost(entries[i].host, &cancel);
            entries[i].checked_at = sceKernelGetProcessTimeWide();
            /* Zero is reserved for pending; negative/local/cancel outcomes stay distinct. */
            __atomic_store_n(&entries[i].result, result == 0 ? 2 : result, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&done, 1, __ATOMIC_RELEASE);
    }
    virtual void Finish() {}
    bool RefreshDue() const
    {
        return __atomic_load_n(&done, __ATOMIC_ACQUIRE) &&
               sceKernelGetProcessTimeWide() - started_at >= 15000000;
    }
    bool Available(const Entry &entry) const
    {
        if (moonlight_is_cancelled(&cancel)) return false;
        int result = __atomic_load_n(&entry.result, __ATOMIC_ACQUIRE);
        uint64_t checked_at;
        if (result == 0) {
            if (!entry.previous_available) return false;
            checked_at = entry.previous_checked_at;
        } else {
            if (result != 1) return false;
            checked_at = entry.checked_at;
        }
        return sceKernelGetProcessTimeWide() - checked_at < 30000000;
    }
    MoonlightBackend &backend;
    MoonlightCancellation cancel;
    int done;
    const uint64_t started_at;
    paf::vector<Entry> entries;
};

HostService::~HostService()
{
    Shutdown();
}

void HostService::CheckAvailability(void *owner, const paf::vector<MoonlightHost> &hosts)
{
    if (hosts.empty()) { StopAvailability(owner); return; }
    AvailabilityJob *old = static_cast<AvailabilityJob *>(m_job.get());
    bool unchanged = m_owner == owner && old && old->entries.size() == hosts.size();
    for (size_t i = 0; unchanged && i < hosts.size(); ++i) {
        unchanged = same_probe_host(old->entries[i].host, hosts[i]);
    }
    if (unchanged && !old->RefreshDue()) return;
    if (!m_queue) {
        paf::job::JobQueue::Option option;
        option.workerNum = 1;
        option.workerStackSize = 64 * 1024;
        m_queue = new paf::job::JobQueue("PC availability", &option);
    }
    paf::common::SharedPtr<paf::job::JobItem> next(
        new AvailabilityJob(m_backend, hosts, unchanged ? old : NULL));
    CancelAvailability();
    m_owner = owner;
    m_job = next;
    if (m_queue->Enqueue(m_job) < 0) {
        AvailabilityJob *job = static_cast<AvailabilityJob *>(m_job.get());
        moonlight_cancel(&job->cancel);
        __atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
    } else {
        m_barrier = m_job;
    }
}

void HostService::StopAvailability(void *owner)
{
    if (m_owner == owner) CancelAvailability();
}

void HostService::CancelAvailability()
{
    if (m_job.get()) moonlight_cancel(&static_cast<AvailabilityJob *>(m_job.get())->cancel);
    m_owner = NULL;
    m_dirty = true;
}

bool HostService::PollAvailability(MoonlightEvent *event)
{
    bool changed = m_dirty;
    m_dirty = false;
    AvailabilityJob *job = static_cast<AvailabilityJob *>(m_job.get());
    for (size_t i = 0; job && i < job->entries.size(); ++i) {
        bool available = job->Available(job->entries[i]);
        if (available != job->entries[i].displayed) changed = true;
        job->entries[i].displayed = available;
    }
    if (changed) {
        memset(event, 0, sizeof(*event));
        event->type = MOONLIGHT_EVENT_HOST_AVAILABILITY_CHANGED;
    }
    return changed;
}

bool HostService::IsAvailable(const MoonlightHost &host) const
{
    AvailabilityJob *job = static_cast<AvailabilityJob *>(m_job.get());
    if (!job) return false;
    for (size_t i = 0; i < job->entries.size(); ++i) {
        if (same_probe_host(job->entries[i].host, host)) return job->Available(job->entries[i]);
    }
    return false;
}

paf::common::SharedPtr<paf::job::JobItem> HostService::AvailabilityBarrier() const
{
    return m_barrier;
}

void HostService::WaitForAvailability(const paf::common::SharedPtr<paf::job::JobItem> &item)
{
    AvailabilityJob *job = static_cast<AvailabilityJob *>(item.get());
    /* Worker only. FIFO single-worker queue means this also drains older cancelled scans. */
    while (job && !__atomic_load_n(&job->done, __ATOMIC_ACQUIRE)) sceKernelDelayThread(10000);
}

void HostService::Shutdown()
{
    CancelAvailability();
    /* Teardown only; ordinary navigation never waits on the PAF thread. */
    if (m_queue) {
        m_queue->WaitEmpty();
        delete m_queue;
        m_queue = NULL;
    }
    m_job.reset();
    m_barrier.reset();
}
