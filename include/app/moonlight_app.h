#ifndef VITA_MOONLIGHT_APP_H
#define VITA_MOONLIGHT_APP_H

#include "services/host_service.h"
#include "services/settings_service.h"

class MoonlightApp {
public:
    static MoonlightApp *Instance();

    int Initialize();
    void Shutdown();

    HostService &Hosts() { return m_hosts; }
    SettingsService &Settings() { return m_settings; }

    bool IsInitialized() const { return m_initialized; }

private:
    MoonlightApp();
    ~MoonlightApp();

    MoonlightApp(const MoonlightApp &);
    MoonlightApp &operator=(const MoonlightApp &);

    bool m_initialized;
    HostService m_hosts;
    SettingsService m_settings;
};

#endif
