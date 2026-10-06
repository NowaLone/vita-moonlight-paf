#ifndef VITA_MOONLIGHT_BACKEND_H
#define VITA_MOONLIGHT_BACKEND_H

#include "moonlight/types.h"

class MoonlightBackend {
public:
    virtual ~MoonlightBackend() {}

    virtual int Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual int OpenSettings() = 0;
    virtual int GetSettings(MoonlightSettings *out) = 0;
    virtual int GetSettingValue(MoonlightSettingKey key, int *out_value) = 0;
    virtual int SetSettingValue(MoonlightSettingKey key, int value) = 0;

    virtual int SetEventCallback(MoonlightEventCallback callback, void *userdata) = 0;

    virtual int GetHosts(MoonlightHost *out, int capacity) = 0;
    virtual int GetDiscoveredHosts(MoonlightHost *out, int capacity) = 0;
    virtual int SearchHosts() = 0;
    virtual int StopHostSearch() = 0;
    virtual int AddHost(const char *address, uint16_t port, const char *name) = 0;
    virtual int MarkHostPaired(const MoonlightHost &host) = 0;
    virtual int DeleteHost(const MoonlightHost &host) = 0;

    virtual int ConnectHost(const MoonlightHost &host) = 0;
    virtual int PreparePairing(char out_pin[5]) = 0;
    virtual int PairCurrentHost(const char pin[5]) = 0;

    virtual int GetApplications(MoonlightApplication *out, int capacity) = 0;
    virtual int StartApplication(int application_id) = 0;
    virtual int StopApplication() = 0;
    virtual int DisconnectHost() = 0;
    virtual MoonlightConnectionState GetConnectionState() const = 0;
};

#endif
