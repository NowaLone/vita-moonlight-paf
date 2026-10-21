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

    virtual ~Job() {}

    virtual void Run()
    {
        if (!moonlight_is_cancelled(&m_cancel)) {
            m_backend.PairCurrentHost(m_pin, &m_cancel);
        }
    }

    virtual void Finish() {}

    void RequestCancel()
    {
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
    if (result != 0) {
        vita_debug_log("[PairingService] enqueue failed result=%d", result);
        Cancel();
    }
    return result;
}
