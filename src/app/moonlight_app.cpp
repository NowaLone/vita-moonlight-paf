#include <paf.h>

#include "app/moonlight_app.h"
#include "moonlight/api.h"

namespace {
MoonlightApp *s_app = NULL;
}

MoonlightApp::MoonlightApp()
    : m_initialized(false),
      m_event_callback(NULL),
      m_event_userdata(NULL),
      m_event_head(0),
      m_event_tail(0),
      m_event_count(0),
      m_event_task_registered(false)
{
}

MoonlightApp::~MoonlightApp()
{
    Shutdown();
}

MoonlightApp *MoonlightApp::Instance()
{
    if (s_app == NULL) {
        s_app = new MoonlightApp();
    }
    return s_app;
}

int MoonlightApp::Initialize()
{
    if (m_initialized) {
        return 0;
    }

    int result = moonlight_api_init();
    if (result != 0) {
        return result;
    }

    m_initialized = true;
    moonlight_api_set_event_callback(OnMoonlightEvent, this);
    return 0;
}

void MoonlightApp::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    moonlight_api_set_event_callback(NULL, NULL);

    paf::thread::RMutex::MainThreadMutex()->Lock();
    m_event_head = 0;
    m_event_tail = 0;
    m_event_count = 0;
    m_event_callback = NULL;
    m_event_userdata = NULL;

    if (m_event_task_registered) {
        m_event_task_registered = false;
        paf::common::MainThreadCallList::Unregister(ProcessEventTask, s_app);
    }

    paf::thread::RMutex::MainThreadMutex()->Unlock();

    moonlight_api_shutdown();
    m_initialized = false;
}

void MoonlightApp::SetEventCallback(MoonlightAppEventCallback callback, void *userdata)
{
    paf::thread::RMutex::MainThreadMutex()->Lock();
    m_event_callback = callback;
    m_event_userdata = userdata;
    paf::thread::RMutex::MainThreadMutex()->Unlock();
}

void MoonlightApp::OnMoonlightEvent(const MoonlightEvent *event, void *userdata)
{
    MoonlightApp *app = (MoonlightApp *)userdata;
    if (!app || !app->m_initialized || !event) {
        return;
    }

    app->QueueEvent(event);
}

void MoonlightApp::QueueEvent(const MoonlightEvent *event)
{
    paf::thread::RMutex::MainThreadMutex()->Lock();

    if (m_event_callback == NULL) {
        paf::thread::RMutex::MainThreadMutex()->Unlock();
        return;
    }

    if (m_event_count >= kEventQueueCapacity) {
        paf::thread::RMutex::MainThreadMutex()->Unlock();
        return;
    }

    QueuedEvent &queued = m_events[m_event_tail];
    queued.event = *event;

    if (event->address != NULL) {
        sce_paf_strncpy(
            queued.address,
            event->address,
            sizeof(queued.address) - 1
        );
        queued.address[sizeof(queued.address) - 1] = '\0';
        queued.event.address = queued.address;
    } else {
        queued.address[0] = '\0';
        queued.event.address = NULL;
    }

    m_event_tail = (m_event_tail + 1) % kEventQueueCapacity;
    m_event_count++;

    if (!m_event_task_registered) {
        m_event_task_registered = true;
        paf::common::MainThreadCallList::Register(ProcessEventTask, this);
    }

    paf::thread::RMutex::MainThreadMutex()->Unlock();
}

void MoonlightApp::ProcessEventTask(void *userdata)
{
    MoonlightApp *app = (MoonlightApp *)userdata;
    if (!app) {
        return;
    }

    while (true) {
        MoonlightEvent event;
        char address[256];

        paf::thread::RMutex::MainThreadMutex()->Lock();

        if (app->m_event_count == 0) {
            app->m_event_task_registered = false;
            paf::common::MainThreadCallList::Unregister(ProcessEventTask, app);
            paf::thread::RMutex::MainThreadMutex()->Unlock();
            return;
        }

        QueuedEvent &queued = app->m_events[app->m_event_head];
        event = queued.event;

        if (queued.event.address != NULL) {
            sce_paf_strncpy(
                address,
                queued.address,
                sizeof(address) - 1
            );
            address[sizeof(address) - 1] = '\0';
            event.address = address;
        } else {
            address[0] = '\0';
            event.address = NULL;
        }

        app->m_event_head = (app->m_event_head + 1) % kEventQueueCapacity;
        app->m_event_count--;

        MoonlightAppEventCallback callback = app->m_event_callback;
        void *callback_userdata = app->m_event_userdata;

        paf::thread::RMutex::MainThreadMutex()->Unlock();

        if (callback) {
            callback(&event, callback_userdata);
        }
    }
}
