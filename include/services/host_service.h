#ifndef VITA_MOONLIGHT_HOST_SERVICE_H
#define VITA_MOONLIGHT_HOST_SERVICE_H

#include <stdint.h>
#include <paf.h>
#include "backend/moonlight_backend.h"

class HostService {
public:
    explicit HostService(MoonlightBackend &backend);

    ~HostService();
    /* PAF main thread: snapshot ownership/state; never probes synchronously. */
    void CheckAvailability(void *owner, const paf::vector<MoonlightHost> &hosts);
    void StopAvailability(void *owner);
    void CancelAvailability();
    bool PollAvailability(MoonlightEvent *event);
    bool IsAvailable(const MoonlightHost &host) const;
    paf::common::SharedPtr<paf::job::JobItem> AvailabilityBarrier() const;
    /* Connect worker only; waits for cancellation cleanup without blocking PAF. */
    static void WaitForAvailability(const paf::common::SharedPtr<paf::job::JobItem> &job);
    void Shutdown();

    int GetHosts(MoonlightHost *out, int capacity);
    int Add(const char *address, uint16_t port, const char *name);
    int Delete(const MoonlightHost &host);

private:
    class AvailabilityJob;
    MoonlightBackend &m_backend;
    paf::job::JobQueue *m_queue;
    paf::common::SharedPtr<paf::job::JobItem> m_job;
    paf::common::SharedPtr<paf::job::JobItem> m_barrier;
    void *m_owner;
    bool m_dirty;
};

#endif
