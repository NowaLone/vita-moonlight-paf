#include <paf.h>

#include "app/moonlight_app.h"
#include "common.h"
#include "pages/page_main.h"
#include <psp2/apputil.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

namespace {
MoonlightApp *s_app = NULL;

static const int kNetMemorySize = 1 * 1024 * 1024;
static void *s_net_memory = NULL;
static bool s_net_initialized = false;
static bool s_netctl_initialized = false;
static bool s_net_module_loaded = false;

static int InitializeNetwork()
{
    if (s_net_initialized) {
        return 0;
    }

    int result = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (result < 0) {
        return result;
    }
    s_net_module_loaded = true;

    s_net_memory = sce_paf_malloc(kNetMemorySize);
    if (s_net_memory == NULL) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
        s_net_module_loaded = false;
        return -1;
    }

    SceNetInitParam net_param;
    sce_paf_memset(&net_param, 0, sizeof(net_param));
    net_param.memory = s_net_memory;
    net_param.size = kNetMemorySize;
    net_param.flags = 0;

    result = sceNetInit(&net_param);
    if (result < 0) {
        sce_paf_free(s_net_memory);
        s_net_memory = NULL;
        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
        s_net_module_loaded = false;
        return result;
    }
    s_net_initialized = true;

    result = sceNetCtlInit();
    if (result < 0) {
        sceNetTerm();
        s_net_initialized = false;

        sce_paf_free(s_net_memory);
        s_net_memory = NULL;

        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
        s_net_module_loaded = false;
        return result;
    }
    s_netctl_initialized = true;

    return 0;
}

static void ShutdownNetwork()
{
    if (s_netctl_initialized) {
        sceNetCtlTerm();
        s_netctl_initialized = false;
    }

    if (s_net_initialized) {
        sceNetTerm();
        s_net_initialized = false;
    }

    if (s_net_memory != NULL) {
        sce_paf_free(s_net_memory);
        s_net_memory = NULL;
    }

    if (s_net_module_loaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
        s_net_module_loaded = false;
    }
}
}

MoonlightApp::MoonlightApp()
    : m_initialized(false),
      m_backend(),
      m_hosts(m_backend),
      m_discovery(m_backend),
      m_pairing(m_backend),
      m_connection(m_backend),
      m_applications(m_backend),
      m_settings(m_backend),
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

int MoonlightApp::Start(paf::Plugin *plugin)
{
    if (!plugin) {
        return -1;
    }

    g_plugin = plugin;

    int result = InitializeNetwork();
    if (result < 0) {
        return result;
    }

    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    sce_paf_memset(&init, 0, sizeof(init));
    sce_paf_memset(&boot, 0, sizeof(boot));

    result = sceAppUtilInit(&init, &boot);
    if (result < 0) {
        ShutdownNetwork();
        return result;
    }

    result = Initialize();
    if (result != 0) {
        ShutdownNetwork();
        return result;
    }

    page::Main *mainPage = new page::Main();
    if (!mainPage || !mainPage->IsValid()) {
        delete mainPage;
        Shutdown();
        return -1;
    }

    return 0;
}

int MoonlightApp::Initialize()
{
    if (m_initialized) {
        return 0;
    }

    int result = m_backend.Initialize();
    if (result != 0) {
        return result;
    }

    result = m_backend.SetEventCallback(OnMoonlightEvent, this);
    if (result != 0) {
        m_backend.Shutdown();
        return result;
    }

    m_initialized = true;
    return 0;
}

void MoonlightApp::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    m_backend.SetEventCallback(NULL, NULL);

    paf::thread::RMutex::main_thread_mutex.Lock();
    m_event_head = 0;
    m_event_tail = 0;
    m_event_count = 0;
    m_event_callback = NULL;
    m_event_userdata = NULL;

    if (m_event_task_registered) {
        m_event_task_registered = false;
        paf::common::MainThreadCallList::Unregister(ProcessEventTask, s_app);
    }

    paf::thread::RMutex::main_thread_mutex.Unlock();

    m_backend.Shutdown();
    m_initialized = false;

    ShutdownNetwork();
}

void MoonlightApp::SetEventCallback(MoonlightAppEventCallback callback, void *userdata)
{
    paf::thread::RMutex::main_thread_mutex.Lock();
    m_event_callback = callback;
    m_event_userdata = userdata;
    paf::thread::RMutex::main_thread_mutex.Unlock();
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
    paf::thread::RMutex::main_thread_mutex.Lock();

    if (m_event_callback == NULL) {
        paf::thread::RMutex::main_thread_mutex.Unlock();
        return;
    }

    if (m_event_count >= kEventQueueCapacity) {
        paf::thread::RMutex::main_thread_mutex.Unlock();
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

    paf::thread::RMutex::main_thread_mutex.Unlock();
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

        paf::thread::RMutex::main_thread_mutex.Lock();

        if (app->m_event_count == 0) {
            app->m_event_task_registered = false;
            paf::common::MainThreadCallList::Unregister(ProcessEventTask, app);
            paf::thread::RMutex::main_thread_mutex.Unlock();
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

        paf::thread::RMutex::main_thread_mutex.Unlock();

        if (callback) {
            callback(&event, callback_userdata);
        }
    }
}
