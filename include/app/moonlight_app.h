#ifndef VITA_MOONLIGHT_APP_H
#define VITA_MOONLIGHT_APP_H

#include "services/host_service.h"
#include "services/settings_service.h"

typedef void (*MoonlightAppEventCallback)(const MoonlightEvent *event, void *userdata);

class MoonlightApp {
public:
    static MoonlightApp *Instance();

    int Start(paf::Plugin *plugin);
    int Initialize();
    void Shutdown();

    HostService &Hosts() { return m_hosts; }
    SettingsService &Settings() { return m_settings; }

    void SetEventCallback(MoonlightAppEventCallback callback, void *userdata);

    bool IsInitialized() const { return m_initialized; }

private:
    struct QueuedEvent {
        MoonlightEvent event;
        char address[256];
    };

    MoonlightApp();
    ~MoonlightApp();

    MoonlightApp(const MoonlightApp &);
    MoonlightApp &operator=(const MoonlightApp &);

    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    static void ProcessEventTask(void *userdata);

    void QueueEvent(const MoonlightEvent *event);

    static const int kEventQueueCapacity = 16;

    bool m_initialized;
    HostService m_hosts;
    SettingsService m_settings;

    MoonlightAppEventCallback m_event_callback;
    void *m_event_userdata;

    QueuedEvent m_events[kEventQueueCapacity];
    int m_event_head;
    int m_event_tail;
    int m_event_count;
    bool m_event_task_registered;
};

#endif
