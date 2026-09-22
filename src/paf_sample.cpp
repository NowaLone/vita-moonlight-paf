#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <paf.h>
#include <psp2/kernel/threadmgr.h>

paf::Plugin *g_plugin = NULL;
paf::ui::Scene *g_current_scene = NULL;

// Forward declarations
static void onCloseSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);
static void onSpeechBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);

class SettingsItemFactory : public paf::ui::listview::ItemFactory {
public:
    SettingsItemFactory() {}
    ~SettingsItemFactory() {}

    paf::ui::ListItem *Create(CreateParam& param);

    void Start(StartParam& param) {
        param.list_item->Show(paf::common::transition::Type_FadeinSlow);
    }

    void Stop(StopParam& param) {
        param.list_item->Hide(paf::common::transition::Type_FadeinSlow);
    }
};

static void onSettingsListItemClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    // Handle individual settings item clicks here
}

paf::ui::ListItem *SettingsItemFactory::Create(CreateParam& param) {
    paf::Plugin::TemplateOpenParam openParam;
    int res = g_plugin->TemplateOpen(param.parent, "template_settings_list_item", openParam);
    if(res != 0) {
        return NULL;
    }

    paf::ui::ListItem *list_item = (paf::ui::ListItem *)param.parent->GetChild(param.parent->GetChildrenNum() - 1);
    paf::ui::Widget *button = list_item->FindChild("button");

    const wchar_t *settings_items[] = {
        L"Video Settings",
        L"Audio Settings",
        L"Network Settings",
        L"Input Settings"
    };

    if(param.cell_index < 4) {
        button->SetString(settings_items[param.cell_index]);
        button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSettingsListItemClick, (void*)(uintptr_t)param.cell_index);
    }

    return list_item;
}

static void onSearchPCsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        paf::Plugin::PageOpenParam pageOpenParam;
        pageOpenParam.option = paf::Plugin::PageOption_None;
        pageOpenParam.fade = true;
        pageOpenParam.fade_time_ms = 200.0f;
        pageOpenParam.transition_type = paf::Plugin::TransitionType_SlideFromBottom;
        g_current_scene = g_plugin->PageOpen("page_search_pcs", pageOpenParam);
    }
}

static void onAddManuallyButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        paf::Plugin::PageOpenParam pageOpenParam;
        pageOpenParam.option = paf::Plugin::PageOption_None;
        pageOpenParam.fade = true;
        pageOpenParam.fade_time_ms = 200.0f;
        pageOpenParam.transition_type = paf::Plugin::TransitionType_SlideFromBottom;
        g_current_scene = g_plugin->PageOpen("page_add_manually", pageOpenParam);
    }
}

static void onSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        paf::Plugin::PageOpenParam pageOpenParam;
        pageOpenParam.option = paf::Plugin::PageOption_None;
        pageOpenParam.fade = true;
        pageOpenParam.fade_time_ms = 300.0f;
        pageOpenParam.transition_type = paf::Plugin::TransitionType_SlideFromRight;
        g_current_scene = g_plugin->PageOpen("page_settings_bubble", pageOpenParam);
        
        if(g_current_scene != NULL) {
            paf::ui::Widget *speech_button = g_current_scene->FindChild("btn_settings_balloon");
            paf::ui::Plane *speech_balloon = (paf::ui::Plane *)g_current_scene->FindChild("settings_speech_balloon");
            if(speech_balloon) {
                paf::common::transition::Do(0.0f, speech_balloon, paf::common::transition::Type_Popup5, false, false);
            }
            if(speech_button)
                ((paf::ui::ButtonBase*)speech_button)->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSpeechBalloonClick);
        }
    }
}

static void onSpeechBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        // Close the speech balloon first
        if(g_current_scene != NULL) {
            paf::ui::Plane *speech_balloon = (paf::ui::Plane *)g_current_scene->FindChild("settings_speech_balloon");
            if(speech_balloon) {
                paf::common::transition::DoReverse(0.0f, speech_balloon, paf::common::transition::Type_Popup5, false, false);
                paf::Timer *pTimer = new paf::Timer(300.0f, paf::Timer::Func::FUNC_BOUNCE_IN);
                speech_balloon->Hide(pTimer, 2);
            }
        }

        // Then open the full settings page
        paf::Plugin::PageOpenParam pageOpenParam;
        pageOpenParam.option = paf::Plugin::PageOption_None;
        pageOpenParam.fade = true;
        pageOpenParam.fade_time_ms = 200.0f;
        pageOpenParam.transition_type = paf::Plugin::TransitionType_SlideFromBottom;
        g_current_scene = g_plugin->PageOpen("page_settings", pageOpenParam);
        
        if(g_current_scene != NULL) {
            paf::ui::ListView *settings_list_view = (paf::ui::ListView *)g_current_scene->FindChild("settings_list_view");
            if(settings_list_view) {
                settings_list_view->SetItemFactory(new SettingsItemFactory());
                settings_list_view->InsertSegment(0, 1);
                settings_list_view->SetCellSizeDefault(0, { 880.0f, 70.0f, 0.0f, 0.0f });
                settings_list_view->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
                settings_list_view->InsertCell(0, 0, 4);
            }

            paf::ui::Widget *btn_back_settings = g_current_scene->FindChild("btn_back_settings");
            if(btn_back_settings) {
                ((paf::ui::CornerButton *)btn_back_settings)->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onCloseSettingsButtonClick);
            }
        }
    }
}

static void onCloseSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        paf::Plugin::PageCloseParam pageCloseParam;
        pageCloseParam.fade = true;
        pageCloseParam.fade_time_ms = 200.0f;
        pageCloseParam.transition_type = paf::Plugin::TransitionType_SlideFromBottom;
        g_plugin->PageClose("page_settings", pageCloseParam);
    }
}

static void onCloseSearchButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        paf::Plugin::PageCloseParam pageCloseParam;
        pageCloseParam.fade = true;
        pageCloseParam.fade_time_ms = 200.0f;
        pageCloseParam.transition_type = paf::Plugin::TransitionType_SlideFromBottom;
        g_plugin->PageClose("page_search_pcs", pageCloseParam);
    }
}

static void onCloseAddManualButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    if(g_plugin) {
        paf::Plugin::PageCloseParam pageCloseParam;
        pageCloseParam.fade = true;
        pageCloseParam.fade_time_ms = 200.0f;
        pageCloseParam.transition_type = paf::Plugin::TransitionType_SlideFromBottom;
        g_plugin->PageClose("page_add_manually", pageCloseParam);
    }
}

void loadPluginCB(paf::Plugin *plugin) {
    g_plugin = plugin;
}

int paf_sample_main(void) {
    paf::Framework::InitParam fwParam;
    fwParam.mode = paf::Framework::Mode_Normal;
    
    paf::Framework *paf_fw = new paf::Framework(fwParam);
    if(paf_fw == NULL)
        exit(0);
    
    paf_fw->LoadCommonResourceSync();
    
    paf::Plugin::InitParam pluginParam;
    pluginParam.name          = "vita_moonlight_ui";
    pluginParam.caller_name   = "__main__";
    pluginParam.resource_file = "app0:/vita_moonlight_ui.rco";
    pluginParam.start_func    = loadPluginCB;
    
    paf::Plugin::LoadSync(pluginParam);
    
    if(g_plugin != NULL) {
        paf::Plugin::PageOpenParam pageOpenParam;
        pageOpenParam.option = paf::Plugin::PageOption_None;
        g_current_scene = g_plugin->PageOpen("page_main", pageOpenParam);
        
        if(g_current_scene != NULL) {
            // Find and setup button callbacks
            paf::ui::Widget *btn_search_pcs = g_current_scene->FindChild("btn_search_pcs");
            paf::ui::Widget *btn_add_manually = g_current_scene->FindChild("btn_add_manually");
            paf::ui::Widget *settings_button = g_current_scene->FindChild("settings_button");

            if(btn_search_pcs)
                ((paf::ui::ButtonBase*)btn_search_pcs)->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSearchPCsButtonClick);
            if(btn_add_manually)
                ((paf::ui::ButtonBase*)btn_add_manually)->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onAddManuallyButtonClick);
            if(settings_button)
                ((paf::ui::CornerButton*)settings_button)->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSettingsButtonClick);
        }
    }
    
    paf_fw->Run();
    exit(0);
    
    return 0;
}
