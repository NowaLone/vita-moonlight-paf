#ifndef VITA_MOONLIGHT_LEGACY_MOONLIGHT_ADAPTER_H
#define VITA_MOONLIGHT_LEGACY_MOONLIGHT_ADAPTER_H

#include "backend/moonlight_backend.h"

class LegacyMoonlightAdapter : public MoonlightBackend {
public:
    LegacyMoonlightAdapter();
    virtual ~LegacyMoonlightAdapter();

    virtual int Initialize();
    virtual void Shutdown();

    virtual int OpenSettings();
    virtual int GetSettings(MoonlightSettings *out);
    virtual int GetSettingValue(MoonlightSettingKey key, int *out_value);
    virtual int SetSettingValue(MoonlightSettingKey key, int value);

    virtual int SetEventCallback(MoonlightEventCallback callback, void *userdata);

    virtual int GetHosts(MoonlightHost *out, int capacity);
    virtual int SearchHosts();
    virtual int AddHost(const char *address, uint16_t port, const char *name);

    virtual int ConnectHost(const MoonlightHost &host);
    virtual int PairCurrentHost();

    virtual int GetApplications(MoonlightApplication *out, int capacity);
    virtual int StartApplication(int application_id);
    virtual int StopApplication();
    virtual int DisconnectHost();
    virtual MoonlightConnectionState GetConnectionState() const;
};

#endif
