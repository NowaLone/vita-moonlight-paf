#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <paf.h>

#include "common.h"
#include "pages/page_main.h"
#include "pages/page_settings.h"
#include "moonlight/api.h"

static void loadPluginCB(paf::Plugin *plugin) {
    g_plugin = plugin;
    if (g_plugin != NULL) {
        new page::Main();
    }
}

int paf_sample_main(void) {
    if (moonlight_api_init() != 0) {
        return -1;
    }

    paf::Framework::InitParam fwParam;
    fwParam.mode = paf::Framework::Mode_Normal;
    paf::Framework::SampleInit(&fwParam);
    fwParam.graphics_option = 7;
    paf::Framework *paf_fw = new paf::Framework(fwParam);
    if (paf_fw == NULL) {
        moonlight_api_shutdown();
        return -1;
    }

    paf_fw->LoadCommonResourceSync();
    page::SettingsPadInit();

    paf::Plugin::InitParam pluginParam;
    pluginParam.name          = "vita_moonlight_ui";
    pluginParam.caller_name   = "__main__";
    pluginParam.resource_file = "app0:/vita_moonlight_ui.rco";
    pluginParam.init_func     = NULL;
    pluginParam.start_func    = loadPluginCB;
    pluginParam.stop_func     = NULL;
    pluginParam.exit_func     = NULL;

    paf::Plugin::LoadSync(pluginParam);
    paf_fw->Run();
    moonlight_api_shutdown();
    return 0;
}
