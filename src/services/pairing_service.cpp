#include <string.h>

#include <paf.h>

#include "services/pairing_service.h"
#include "debug.h"

class PairingService::Job : public paf::job::JobItem {
public:
    Job(MoonlightBackend &backend, const char pin[5])
        : paf::job::JobItem("PairingService::PairingJob", NULL),
          m_backend(backend)
    {
        memcpy(m_pin, pin, sizeof(m_pin));
        m_cancel.requested = 0; /* Before publishing the job to any worker. */
    }

    virtual ~Job()
    {
        vita_debug_log("[PairingService] job=%p destroyed", (void *)this);
    }

    virtual void Run()
    {
        vita_debug_log("[PairingService] job=%p Run begin", (void *)this);
        int result = MOONLIGHT_PAIRING_CANCELLED;
        if (!moonlight_is_cancelled(&m_cancel)) {
            result = m_backend.PairCurrentHost(m_pin, &m_cancel);
        }
        vita_debug_log("[PairingService] job=%p Run returned result=%d", (void *)this, result);
    }

    virtual void Finish()
    {
        vita_debug_log("[PairingService] job=%p Finish", (void *)this);
    }

    void RequestCancel()
    {
        vita_debug_log("[PairingService] job=%p cancel requested", (void *)this);
        moonlight_cancel(&m_cancel);
    }

private:
    MoonlightBackend &m_backend;
    char m_pin[5];
    MoonlightCancellation m_cancel;
};

PairingService::PairingService(MoonlightBackend &backend)
    : m_backend(backend)
{
}

PairingService::~PairingService()
{
    Cancel();
}

void PairingService::Cancel()
{
    if (m_job.get()) {
        static_cast<Job *>(m_job.get())->RequestCancel();
        /* The queue retains its reference until Run/Finish return. */
        m_job.reset();
    }
}

int PairingService::Prepare(char out_pin[5])
{
    return m_backend.PreparePairing(out_pin);
}

int PairingService::Pair(const char pin[5])
{
    if (!pin || strlen(pin) != 4) {
        return -1;
    }

    if (paf::job::JobQueue::default_queue == NULL) {
        return -1;
    }

    Cancel();
    paf::common::SharedPtr<paf::job::JobItem> item(
        new Job(m_backend, pin)
    );

    m_job = item;
    int result = paf::job::JobQueue::default_queue->Enqueue(item);
    vita_debug_log("[PairingService] enqueue result=%d", result);
    if (result != 0) Cancel();
    return result;
}
