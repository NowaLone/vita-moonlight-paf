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
    virtual int GetDiscoveredHosts(MoonlightHost *out, int capacity);
    virtual int SearchHosts();
    virtual int StopHostSearch();
    virtual int AddHost(const char *address, uint16_t port, const char *name);
    virtual int MarkHostPaired(const MoonlightHost &host);

    virtual int ConnectHost(const MoonlightHost &host);
    virtual int PreparePairing(char out_pin[5]);
    virtual int PairCurrentHost(const char pin[5]);

    virtual int GetApplications(MoonlightApplication *out, int capacity);
    virtual int StartApplication(int application_id);
    virtual int StopApplication();
    virtual int DisconnectHost();
    virtual MoonlightConnectionState GetConnectionState() const;

private:
    static void OnLegacyEvent(const MoonlightEvent *event, void *userdata);
    void ForwardEvent(const MoonlightEvent *event);

    MoonlightConnectionState m_connection_state;
    MoonlightHost m_current_host;
    bool m_has_current_host;
    MoonlightEventCallback m_event_callback;
    void *m_event_userdata;
};

#endif
