#ifndef VITA_MOONLIGHT_BACKEND_H
#define VITA_MOONLIGHT_BACKEND_H

#include "moonlight/types.h"
#include "moonlight/cancellation.h"
#include "moonlight/stream_stats.h"
#include "moonlight/controller_light.h"

class MoonlightBackend {
public:
    virtual ~MoonlightBackend() {}

    virtual int Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual int OpenSettings() = 0;

    virtual int SetEventCallback(MoonlightEventCallback callback, void *userdata) = 0;

    virtual int GetHosts(MoonlightHost *out, int capacity) = 0;
    virtual int GetDiscoveredHosts(MoonlightHost *out, int capacity) = 0;
    /* Start/Stop are serialized lifecycle calls. SearchHosts starts a backend-owned
     * worker without doing the network scan on the caller thread; returns start
     * failures directly. The caller owns presentation of a synchronous failure. */
    virtual int SearchHosts() = 0;
    virtual int StopHostSearch() = 0;
    virtual int AddHost(const char *address, uint16_t port, const char *name) = 0;
    virtual int DeleteHost(const MoonlightHost &host) = 0;

    virtual int ConnectHost(const MoonlightHost &host) = 0;
    virtual int PreparePairing(char out_pin[5]) = 0;
    virtual int PairCurrentHost(const char pin[5], const MoonlightCancellation *cancel) = 0;

    virtual int GetApplications(MoonlightApplication *out, int capacity) = 0;
    /* Nonzero quit ID is supplied only after explicit remote-quit confirmation. */
    virtual int StartApplication(int application_id, int quit_application_id = 0) = 0;
    virtual int StopApplication() = 0;
    virtual int DisconnectHost() = 0;
    virtual MoonlightConnectionState GetConnectionState() const = 0;
    virtual int GetStreamStats(MoonlightStreamStats *out) = 0;
    virtual int GetControllerLight(MoonlightControllerLight *out) = 0;
};

#endif
