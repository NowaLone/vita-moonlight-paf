#ifndef VITA_MOONLIGHT_PAIRING_SERVICE_H
#define VITA_MOONLIGHT_PAIRING_SERVICE_H

#include <paf.h>
#include "backend/moonlight_backend.h"

class PairingService {
public:
    explicit PairingService(MoonlightBackend &backend);
    ~PairingService();

    int Prepare(char out_pin[5]);
    int Pair(const char pin[5]);
    /* Main-thread request only: never joins a worker or takes its HTTP mutex. */
    void Cancel();

private:
    class Job;
    MoonlightBackend &m_backend;
    paf::common::SharedPtr<paf::job::JobItem> m_job;
};

#endif
