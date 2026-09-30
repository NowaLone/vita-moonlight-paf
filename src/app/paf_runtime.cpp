#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <paf.h>\n#include <psp2/kernel/clib.h>

#include "app/moonlight_app.h"

static void loadPluginCB(paf::Plugin *plugin)
{
    MoonlightApp::Instance()->Start(plugin);
}

int paf_runtime_main(void)
{
    sceClibPrintf("[PSP2SHELL] Framework::SampleInit\\n");\n    paf::Framework::InitParam fwParam;
    fwParam.mode = paf::Framework::Mode_Normal;
    paf::Framework::SampleInit(&fwParam);
    fwParam.graphics_option = 7;

    paf::Framework *paf_fw = new paf::Framework(fwParam);\n    sceClibPrintf("[PSP2SHELL] Framework created: %p\\n", paf_fw);
    if (paf_fw == NULL) {
        return -1;
    }

    sceClibPrintf("[PSP2SHELL] Loading common PAF resources\\n");\n    paf_fw->LoadCommonResourceSync();\n    sceClibPrintf("[PSP2SHELL] Common PAF resources loaded\\n");

    paf::Plugin::InitParam pluginParam;
    pluginParam.name = "vita_moonlight_ui";
    pluginParam.caller_name = "__main__";
    pluginParam.resource_file = "app0:/vita_moonlight_ui.rco";
    pluginParam.init_func = NULL;
    pluginParam.start_func = loadPluginCB;
    pluginParam.stop_func = NULL;
    pluginParam.exit_func = NULL;

    sceClibPrintf("[PSP2SHELL] Loading plugin: %s\\n", pluginParam.name);\n    paf::Plugin::LoadSync(pluginParam);\n    sceClibPrintf("[PSP2SHELL] Plugin LoadSync returned\\n");
    sceClibPrintf("[PSP2SHELL] Entering PAF Framework::Run\\n");\n    paf_fw->Run();\n    sceClibPrintf("[PSP2SHELL] Framework::Run returned\\n");
    MoonlightApp::Instance()->Shutdown();
    return 0;
}
