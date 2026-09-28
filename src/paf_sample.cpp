#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <psp2/sysmodule.h>
#include <psp2/apputil.h>
#include <paf.h>

#include "common.h"
#include "pages/page_main.h"
#include "moonlight/api.h"

static void onMoonlightEvent(const MoonlightEvent *event, void *)
{
    if (!event) return;

    if (event->type == MOONLIGHT_EVENT_SETTINGS_CLOSED) {
        page::Main *main = page::Main::Instance();
        if (main) {
            main->RestoreAfterSystemSettings();
        }
    }
}

static void loadPluginCB(paf::Plugin *plugin)
{
    if (!plugin) {
        return;
    }

    g_plugin = plugin;

    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    sce_paf_memset(&init, 0, sizeof(init));
    sce_paf_memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);

    moonlight_api_init();
    moonlight_api_set_event_callback(onMoonlightEvent, NULL);

    page::Main *mainPage = new page::Main();
    if (!mainPage->IsValid()) {
        delete mainPage;
    }
}

int paf_sample_main(void)
{
    paf::Framework::InitParam fwParam;
    fwParam.mode = paf::Framework::Mode_Normal;
    paf::Framework::SampleInit(&fwParam);
    fwParam.graphics_option = 7;

    paf::Framework *paf_fw = new paf::Framework(fwParam);
    if (paf_fw == NULL) {
        return -1;
    }

    paf_fw->LoadCommonResourceSync();

    paf::Plugin::InitParam pluginParam;
    pluginParam.name = "vita_moonlight_ui";
    pluginParam.caller_name = "__main__";
    pluginParam.resource_file = "app0:/vita_moonlight_ui.rco";
    pluginParam.init_func = NULL;
    pluginParam.start_func = loadPluginCB;
    pluginParam.stop_func = NULL;
    pluginParam.exit_func = NULL;

    paf::Plugin::LoadSync(pluginParam);
    paf_fw->Run();
    moonlight_api_shutdown();
    return 0;
}
