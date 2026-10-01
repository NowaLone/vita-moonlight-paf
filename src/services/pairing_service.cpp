#include <string.h>

#include <paf.h>

#include "services/pairing_service.h"

namespace {

class PairingJob : public paf::job::JobItem {
public:
    PairingJob(MoonlightBackend &backend, const char pin[5])
        : paf::job::JobItem("PairingService::PairingJob", NULL),
          m_backend(backend)
    {
        memcpy(m_pin, pin, sizeof(m_pin));
    }

    virtual ~PairingJob() {}

    virtual void Run()
    {
        m_backend.PairCurrentHost(m_pin);
    }

    virtual void Finish() {}

private:
    MoonlightBackend &m_backend;
    char m_pin[5];
};

}

PairingService::PairingService(MoonlightBackend &backend)
    : m_backend(backend)
{
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

    paf::common::SharedPtr<paf::job::JobItem> item(
        new PairingJob(m_backend, pin)
    );

    return paf::job::JobQueue::default_queue->Enqueue(item);
}
