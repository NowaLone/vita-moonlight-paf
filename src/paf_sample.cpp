#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <paf.h>
#include <psp2/kernel/threadmgr.h>

paf::Plugin *g_plugin = NULL;
paf::ui::Scene *g_current_scene = NULL;

static const int kMaxPageDepth = 8;
static const char *g_page_stack[kMaxPageDepth];
static int g_page_depth = 0;

typedef void (*DecideCb)(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);

static void onCloseSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);
static void onCloseSearchButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);
static void onCloseAddManualButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);
static void onSpeechBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);
static void onDismissBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);

static paf::Plugin::PageOpenParam make_open_param(paf::Plugin::TransitionType transition) {
    paf::Plugin::PageOpenParam param;
    param.option = paf::Plugin::PageOption_None;
    param.fade = true;
    param.fade_time_ms = 200.0f;
    param.transition_type = transition;
    return param;
}

static paf::Plugin::PageCloseParam make_close_param(paf::Plugin::TransitionType transition) {
    paf::Plugin::PageCloseParam param;
    param.fade = true;
    param.fade_time_ms = 200.0f;
    param.transition_type = transition;
    return param;
}

static int id_eq(const char *a, const char *b) {
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    return sce_paf_strcmp(a, b) == 0;
}

static bool page_is_open(const char *id) {
    for (int i = 0; i < g_page_depth; i++) {
        if (id_eq(g_page_stack[i], id)) {
            return true;
        }
    }
    return false;
}

static void bind_decide(paf::ui::Widget *root, const char *child_id, DecideCb cb) {
    if (root == NULL || child_id == NULL || cb == NULL) {
        return;
    }
    paf::ui::Widget *child = root->FindChild(child_id);
    if (child != NULL) {
        child->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, cb);
    }
}

static paf::ui::Scene *open_page(const char *id, paf::Plugin::TransitionType transition) {
    if (g_plugin == NULL || id == NULL || g_page_depth >= kMaxPageDepth) {
        return NULL;
    }
    if (page_is_open(id)) {
        return g_current_scene;
    }

    paf::ui::Scene *scene = g_plugin->PageOpen(id, make_open_param(transition));
    if (scene == NULL) {
        return NULL;
    }

    g_page_stack[g_page_depth] = id;
    g_page_depth++;
    g_current_scene = scene;
    return scene;
}

static void close_page(const char *id, paf::Plugin::TransitionType transition) {
    if (g_plugin == NULL || id == NULL || g_page_depth <= 1) {
        return;
    }
    if (id_eq(g_page_stack[0], id)) {
        return;
    }
    if (!page_is_open(id)) {
        return;
    }

    g_plugin->PageClose(id, make_close_param(transition));

    int write = 0;
    for (int read = 0; read < g_page_depth; read++) {
        if (!id_eq(g_page_stack[read], id)) {
            g_page_stack[write++] = g_page_stack[read];
        }
    }
    g_page_depth = write;
    g_current_scene = NULL;
}

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
}

paf::ui::ListItem *SettingsItemFactory::Create(CreateParam& param) {
    paf::Plugin::TemplateOpenParam openParam;
    int res = g_plugin->TemplateOpen(param.parent, "template_settings_list_item", openParam);
    if (res != 0) {
        return NULL;
    }

    paf::ui::ListItem *list_item = (paf::ui::ListItem *)param.parent->GetChild(param.parent->GetChildrenNum() - 1);
    if (list_item == NULL) {
        return NULL;
    }

    paf::ui::Widget *button = list_item->FindChild("button");
    if (button == NULL) {
        return list_item;
    }

    const wchar_t *settings_items[] = {
        L"Video Settings",
        L"Audio Settings",
        L"Network Settings",
        L"Input Settings"
    };

    if (param.cell_index >= 0 && param.cell_index < 4) {
        button->SetString(settings_items[param.cell_index]);
        button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSettingsListItemClick, (void *)(uintptr_t)param.cell_index);
    }

    return list_item;
}

static void setup_settings_page(paf::ui::Scene *scene) {
    if (scene == NULL) {
        return;
    }

    paf::ui::ListView *settings_list_view = (paf::ui::ListView *)scene->FindChild("settings_list_view");
    if (settings_list_view) {
        settings_list_view->SetItemFactory(new SettingsItemFactory());
        settings_list_view->InsertSegment(0, 1);
        settings_list_view->SetCellSizeDefault(0, { 880.0f, 70.0f, 0.0f, 0.0f });
        settings_list_view->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
        settings_list_view->InsertCell(0, 0, 4);
    }

    bind_decide(scene, "btn_back_settings", onCloseSettingsButtonClick);
}

static void onSearchPCsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    paf::ui::Scene *scene = open_page("page_search_pcs", paf::Plugin::TransitionType_SlideFromBottom);
    bind_decide(scene, "btn_close_search", onCloseSearchButtonClick);
}

static void onAddManuallyButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    paf::ui::Scene *scene = open_page("page_add_manually", paf::Plugin::TransitionType_SlideFromBottom);
    bind_decide(scene, "btn_close_add_manual", onCloseAddManualButtonClick);
}

static void onSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    paf::ui::Scene *scene = open_page("page_settings_bubble", paf::Plugin::TransitionType_None);
    if (scene == NULL) {
        return;
    }

    paf::ui::Plane *speech_balloon = (paf::ui::Plane *)scene->FindChild("settings_speech_balloon");
    if (speech_balloon) {
        paf::common::transition::Do(0.0f, speech_balloon, paf::common::transition::Type_Popup5, false, false);
    }

    bind_decide(scene, "btn_settings_balloon", onSpeechBalloonClick);
    bind_decide(scene, "btn_dismiss_balloon", onDismissBalloonClick);
}

static void onDismissBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    close_page("page_settings_bubble", paf::Plugin::TransitionType_None);
}

static void onSpeechBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    close_page("page_settings_bubble", paf::Plugin::TransitionType_None);
    paf::ui::Scene *scene = open_page("page_settings", paf::Plugin::TransitionType_SlideFromBottom);
    setup_settings_page(scene);
}

static void onCloseSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    close_page("page_settings", paf::Plugin::TransitionType_SlideFromBottom);
}

static void onCloseSearchButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    close_page("page_search_pcs", paf::Plugin::TransitionType_SlideFromBottom);
}

static void onCloseAddManualButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    close_page("page_add_manually", paf::Plugin::TransitionType_SlideFromBottom);
}

void loadPluginCB(paf::Plugin *plugin) {
    g_plugin = plugin;
}

int paf_sample_main(void) {
    paf::Framework::InitParam fwParam;
    fwParam.mode = paf::Framework::Mode_Normal;

    paf::Framework *paf_fw = new paf::Framework(fwParam);
    if (paf_fw == NULL) {
        return -1;
    }

    paf_fw->LoadCommonResourceSync();

    paf::Plugin::InitParam pluginParam;
    pluginParam.name          = "vita_moonlight_ui";
    pluginParam.caller_name   = "__main__";
    pluginParam.resource_file = "app0:/vita_moonlight_ui.rco";
    pluginParam.init_func     = NULL;
    pluginParam.start_func    = loadPluginCB;
    pluginParam.stop_func     = NULL;
    pluginParam.exit_func     = NULL;

    paf::Plugin::LoadSync(pluginParam);

    if (g_plugin != NULL) {
        paf::ui::Scene *scene = open_page("page_main", paf::Plugin::TransitionType_None);
        if (scene != NULL) {
            bind_decide(scene, "btn_search_pcs", onSearchPCsButtonClick);
            bind_decide(scene, "btn_add_manually", onAddManuallyButtonClick);
            bind_decide(scene, "settings_button", onSettingsButtonClick);
        }
    }

    paf_fw->Run();
    return 0;
}
