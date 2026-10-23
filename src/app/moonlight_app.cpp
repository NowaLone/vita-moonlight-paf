#include <paf.h>

#include "app/moonlight_app.h"
#include "app/memory_budget.h"
#include "common.h"
#include "debug.h"
#ifdef MOONLIGHT_DISCONNECT_TEST
#include "app/disconnect_test.h"
#endif
#include "pages/page_main.h"
#include <psp2/appmgr.h>
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

static int set_infobar_visible(bool visible)
{
    int result = sceAppMgrSetInfobarState(
        visible ? SCE_APPMGR_INFOBAR_VISIBILITY_VISIBLE
                : SCE_APPMGR_INFOBAR_VISIBILITY_INVISIBLE,
        SCE_APPMGR_INFOBAR_COLOR_BLACK,
        SCE_APPMGR_INFOBAR_TRANSPARENCY_TRANSLUCENT);

    vita_debug_log(
        "[Infobar] request visible=%d result=0x%08x",
        visible ? 1 : 0, (unsigned int)result);
    return result;
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
        ShutdownNetwork();
        return -1;
    }

    SceNetInitParam net_param;
    sce_paf_memset(&net_param, 0, sizeof(net_param));
    net_param.memory = s_net_memory;
    net_param.size = kNetMemorySize;
    net_param.flags = 0;

    result = sceNetInit(&net_param);
    if (result < 0) {
        ShutdownNetwork();
        return result;
    }
    s_net_initialized = true;

    result = sceNetCtlInit();
    if (result < 0) {
        ShutdownNetwork();
        return result;
    }
    s_netctl_initialized = true;

    return 0;
}
}

MoonlightApp::MoonlightApp()
    : m_initialized(false),
      m_infobar_restore_pending(false),
      m_backend(),
      m_hosts(m_backend),
      m_discovery(m_backend),
      m_pairing(m_backend),
      m_connection(m_backend, m_pairing),
      m_applications(m_backend, m_connection),
      m_settings(m_backend),
      m_event_callback(NULL),
      m_event_userdata(NULL),
      m_event_task_registered(false)
{
    moonlight_event_queue_reset(&m_events);
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

    moonlight_log_memory_budget("main-ready");
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

    paf::thread::RMutex::main_thread_mutex.Lock();
    moonlight_event_queue_reset(&m_events);
    m_event_task_registered = true;
    paf::thread::RMutex::main_thread_mutex.Unlock();

    paf::common::MainThreadCallList::Register(ProcessEventTask, this);

    m_initialized = true;
    return 0;
}

void MoonlightApp::Shutdown()
{
#ifdef MOONLIGHT_DISCONNECT_TEST
    disconnect_test_shutdown();
#endif
    /* Shutdown disables event delivery, so STREAM_STOPPED may never reach Apps. */
    RestoreStreamInfobar();

    if (!m_initialized) {
        return;
    }

    m_backend.SetEventCallback(NULL, NULL);

    paf::thread::RMutex::main_thread_mutex.Lock();
    moonlight_event_queue_reset(&m_events);
    m_event_callback = NULL;
    m_event_userdata = NULL;

    bool unregister_event_task = false;
    if (m_event_task_registered) {
        m_event_task_registered = false;
        unregister_event_task = true;
    }

    paf::thread::RMutex::main_thread_mutex.Unlock();

    if (unregister_event_task) {
        paf::common::MainThreadCallList::Unregister(ProcessEventTask, this);
    }

    m_pairing.Cancel();
    m_connection.Shutdown();
    moonlight_log_memory_budget("app-shutdown");
    m_backend.Shutdown();
    m_initialized = false;

    ShutdownNetwork();
}

void MoonlightApp::HideStreamInfobar()
{
    /* Even a failed hide request must be followed by a best-effort restore. */
    m_infobar_restore_pending = true;
    set_infobar_visible(false);
}

void MoonlightApp::RestoreStreamInfobar()
{
    if (!m_infobar_restore_pending) {
        return;
    }

    if (set_infobar_visible(true) >= 0) {
        m_infobar_restore_pending = false;
    }
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

    if (moonlight_event_queue_push(&m_events, event) != 0) {
        /* Snapshot bursts cannot reach this bound. Reject an ordered-only burst
         * beyond the bounded sequential-workflow queue without overwriting it. */
        vita_debug_log("[Events] ordered queue exhausted: event=%d result=%d", (int)event->type, event->result);
    }

    paf::thread::RMutex::main_thread_mutex.Unlock();
}

void MoonlightApp::ProcessEventTask(void *userdata)
{
    MoonlightApp *app = (MoonlightApp *)userdata;
    if (!app) {
        return;
    }
#ifdef MOONLIGHT_DISCONNECT_TEST
    disconnect_test_poll(*app);
#endif

    while (true) {
        MoonlightEvent event;
        char address[256];
        MoonlightAppEventCallback callback;
        void *callback_userdata;

        paf::thread::RMutex::main_thread_mutex.Lock();

        const bool queued_event = moonlight_event_queue_peek(&app->m_events, &event) != 0;
        if (!queued_event && !app->m_connection.GetCompletedJobEvent(&event)) {
            paf::thread::RMutex::main_thread_mutex.Unlock();
            return;
        }

        if (queued_event) {
            if (event.address != NULL) {
                sce_paf_strncpy(
                    address,
                    event.address,
                    sizeof(address) - 1
                );
                address[sizeof(address) - 1] = '\0';
                event.address = address;
            } else {
                address[0] = '\0';
                event.address = NULL;
            }
        }

        callback = app->m_event_callback;
        callback_userdata = app->m_event_userdata;

        paf::thread::RMutex::main_thread_mutex.Unlock();

        if (!app->m_connection.PrepareCompletion(event)) {
            return; /* Retain the owned completion until the backend call returns. */
        }

        paf::thread::RMutex::main_thread_mutex.Lock();
        if (queued_event) {
            moonlight_event_queue_acknowledge(&app->m_events);
        }
        paf::thread::RMutex::main_thread_mutex.Unlock();

#ifdef MOONLIGHT_DISCONNECT_TEST
        disconnect_test_event(*app, event);
#endif
        if (callback) {
            callback(&event, callback_userdata);
        }

        if (event.type == MOONLIGHT_EVENT_CONNECTION_READY) {
            moonlight_log_memory_budget("connection-ready");
        } else if (event.type == MOONLIGHT_EVENT_CONNECTION_FAILED) {
            moonlight_log_memory_budget("connection-failed");
        }
    }
}
