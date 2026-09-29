#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <paf.h>

#include "app/moonlight_app.h"

static void loadPluginCB(paf::Plugin *plugin)
{
    MoonlightApp::Instance()->Start(plugin);
}

int paf_runtime_main(void)
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
    MoonlightApp::Instance()->Shutdown();
    return 0;
}
