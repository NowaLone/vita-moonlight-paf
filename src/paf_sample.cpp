#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <paf.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

paf::Plugin *g_plugin = NULL;
paf::ui::Scene *g_current_scene = NULL;
static paf::ui::Scene *g_main_scene = NULL;

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

static void set_widget_focusable(paf::ui::Widget *widget, bool on) {
    if (widget == NULL) {
        return;
    }
    if (on) {
        widget->EnableEvent(paf::ui::EV_FOCUS);
    } else {
        widget->EnableFocusEvent(false);
        widget->DisableEvent(paf::ui::EV_FOCUS);
        widget->ReleaseFocus();
    }
}

static void set_main_buttons_focusable(bool on) {
    if (g_main_scene == NULL) {
        return;
    }
    const char *ids[] = { "btn_search_pcs", "btn_add_manually", "settings_button" };
    for (int i = 0; i < 3; i++) {
        set_widget_focusable(g_main_scene->FindChild(ids[i]), on);
    }
}

static void close_settings_balloon() {
    close_page("page_settings_bubble", paf::Plugin::TransitionType_None);
    if (!page_is_open("page_settings")) {
        set_main_buttons_focusable(true);
    }
}

class SettingsItemFactory : public paf::ui::listview::ItemFactory {
public:
    SettingsItemFactory() {}
    ~SettingsItemFactory() {}

    paf::ui::ListItem *Create(CreateParam& param);

    void Start(StartParam& param) {
        param.list_item->Show(paf::common::transition::Type_FadeinSlow);
    }

    void Stop(StopParam& param);
};

enum SettingKind {
    KIND_CATEGORY = 0,
    KIND_NOTE,
    KIND_FIXED,
    KIND_TOGGLE,
    KIND_CHOICE,
    KIND_INT
};

struct SettingRow {
    SettingKind kind;
    const wchar_t *label;
    const wchar_t *fixed;
    const wchar_t *const *choices;
    int choice_count;
    int value;
    int step;
    int min_v;
    int max_v;
    int arrows;
};

static const wchar_t *k_resolutions[] = {
    L"960x540", L"960x544", L"1024x576", L"1152x648", L"1280x540",
    L"1280x720", L"1366x768", L"1600x900", L"1920x1080"
};
static const wchar_t *k_fps[] = { L"24", L"30", L"40", L"50", L"60" };
static const wchar_t *k_bitrate[] = {
    L"2000", L"4000", L"5000", L"8000", L"10000", L"15000", L"20000", L"30000"
};
static const wchar_t *k_controller[] = { L"Xbox", L"PlayStation" };
static const wchar_t *k_touch[] = { L"Off", L"DS4 Touchpad", L"Mouse Absolute", L"Tablet (Sunshine)" };
static const wchar_t *k_keyboard[] = { L"US", L"DE", L"ES", L"FR", L"RU" };

static const int kSettingRowCount = 32;
static SettingRow g_rows[32];
static int g_rows_ready = 0;
static paf::ui::Widget *g_row_widgets[32];
static paf::ui::ListView *g_settings_list = NULL;
static int g_focused_row = -1;
static uint32_t g_prev_pad = 0;

static SettingRow make_category(const wchar_t *label) {
    SettingRow row = {};
    row.kind = KIND_CATEGORY;
    row.label = label;
    return row;
}

static SettingRow make_note(const wchar_t *label) {
    SettingRow row = {};
    row.kind = KIND_NOTE;
    row.label = label;
    return row;
}

static SettingRow make_fixed(const wchar_t *label, const wchar_t *value) {
    SettingRow row = {};
    row.kind = KIND_FIXED;
    row.label = label;
    row.fixed = value;
    return row;
}

static SettingRow make_toggle(const wchar_t *label, int value) {
    SettingRow row = {};
    row.kind = KIND_TOGGLE;
    row.label = label;
    row.value = value;
    return row;
}

static SettingRow make_choice(const wchar_t *label, const wchar_t *const *choices, int count, int value, int arrows) {
    SettingRow row = {};
    row.kind = KIND_CHOICE;
    row.label = label;
    row.choices = choices;
    row.choice_count = count;
    row.value = value;
    row.arrows = arrows;
    return row;
}

static SettingRow make_int(const wchar_t *label, int value, int step, int min_v, int max_v, int arrows) {
    SettingRow row = {};
    row.kind = KIND_INT;
    row.label = label;
    row.value = value;
    row.step = step;
    row.min_v = min_v;
    row.max_v = max_v;
    row.arrows = arrows;
    return row;
}

static void init_setting_rows() {
    if (g_rows_ready) {
        return;
    }
    g_rows_ready = 1;

    int i = 0;
    g_rows[i++] = make_category(L"Stream");
    g_rows[i++] = make_choice(L"Resolution", k_resolutions, 9, 1, 1);
    g_rows[i++] = make_choice(L"FPS", k_fps, 5, 4, 1);
    g_rows[i++] = make_choice(L"Bitrate", k_bitrate, 8, 4, 0);
    g_rows[i++] = make_toggle(L"Change graphical game settings for performance", 1);
    g_rows[i++] = make_toggle(L"Enable reference frame invalidation", 0);
    g_rows[i++] = make_toggle(L"Enable stream optimization", 1);
    g_rows[i++] = make_toggle(L"Enable VITA vblank", 0);
    g_rows[i++] = make_toggle(L"Enable frame pacer", 1);
    g_rows[i++] = make_toggle(L"Enable local audio", 0);

    g_rows[i++] = make_category(L"System");
    g_rows[i++] = make_toggle(L"Enable debug log", 1);
    g_rows[i++] = make_toggle(L"Disable power save", 1);
    g_rows[i++] = make_toggle(L"Swap X & O for Moonlight", 0);
    g_rows[i++] = make_toggle(L"Display streaming FPS", 0);

    g_rows[i++] = make_category(L"Input");
    g_rows[i++] = make_toggle(L"Enable Gyroscope reporting", 0);
    g_rows[i++] = make_toggle(L"Enable double tap to sprint", 0);
    g_rows[i++] = make_int(L"Sprint double tap time", 200, 50, 50, 500, 0);
    g_rows[i++] = make_choice(L"Controller type", k_controller, 2, 1, 1);
    g_rows[i++] = make_toggle(L"Swap R1/L1 <-> R2/L2", 0);
    g_rows[i++] = make_int(L"Mouse acceleration", 0, 15, 0, 300, 1);
    g_rows[i++] = make_toggle(L"Enable mapping file", 0);
    g_rows[i++] = make_note(L"Located at ux0:data/moonlight/vita.conf");
    g_rows[i++] = make_note(L"Example in github repo.");
    g_rows[i++] = make_toggle(L"Enable PS button capture", 0);
    g_rows[i++] = make_fixed(L"Back touchscreen deadzone", L"0px,0px,0px,0px");
    g_rows[i++] = make_toggle(L"Enable touchscreen special keys", 0);
    g_rows[i++] = make_fixed(L"Touchscreen special keys", L"...");
    g_rows[i++] = make_choice(L"Touchscreen mode", k_touch, 4, 0, 0);

    g_rows[i++] = make_category(L"Keyboard");
    g_rows[i++] = make_choice(L"Keyboard layout", k_keyboard, 5, 0, 0);
}

static void ascii_to_wide(wchar_t *dst, int cap, const char *src) {
    int i = 0;
    if (cap <= 0) {
        return;
    }
    for (; src[i] != '\0' && i < cap - 1; i++) {
        dst[i] = (wchar_t)src[i];
    }
    dst[i] = 0;
}

static void format_setting_value(const SettingRow& row, wchar_t *dst, int cap) {
    char buf[64];
    if (row.kind == KIND_TOGGLE) {
        ascii_to_wide(dst, cap, row.value ? "yes" : "no");
        return;
    }
    if (row.kind == KIND_CHOICE && row.choices != NULL && row.choice_count > 0) {
        int index = row.value;
        if (index < 0) {
            index = 0;
        }
        if (index >= row.choice_count) {
            index = row.choice_count - 1;
        }
        if (row.arrows) {
            char narrow[48];
            int n = 0;
            const wchar_t *src = row.choices[index];
            for (; src[n] != 0 && n < (int)sizeof(narrow) - 5; n++) {
                narrow[n] = (char)src[n];
            }
            narrow[n++] = ' ';
            narrow[n++] = '<';
            narrow[n++] = '>';
            narrow[n] = '\0';
            ascii_to_wide(dst, cap, narrow);
            return;
        }
        int n = 0;
        const wchar_t *src = row.choices[index];
        for (; src[n] != 0 && n < cap - 1; n++) {
            dst[n] = src[n];
        }
        dst[n] = 0;
        return;
    }
    if (row.kind == KIND_INT) {
        snprintf(buf, sizeof(buf), row.arrows ? "%d <>" : "%d", row.value);
        ascii_to_wide(dst, cap, buf);
        return;
    }
    if (row.kind == KIND_FIXED && row.fixed != NULL) {
        int n = 0;
        for (; row.fixed[n] != 0 && n < cap - 1; n++) {
            dst[n] = row.fixed[n];
        }
        dst[n] = 0;
        return;
    }
    if (cap > 0) {
        dst[0] = 0;
    }
}

static void refresh_setting_value(int index) {
    if (index < 0 || index >= kSettingRowCount || g_row_widgets[index] == NULL) {
        return;
    }
    paf::ui::Widget *value = g_row_widgets[index]->FindChild("value");
    if (value == NULL) {
        return;
    }
    wchar_t text[64];
    format_setting_value(g_rows[index], text, 64);
    value->SetString(text);
}

static void change_setting(int index, int dir) {
    if (index < 0 || index >= kSettingRowCount) {
        return;
    }
    SettingRow& row = g_rows[index];
    if (row.kind == KIND_TOGGLE) {
        if (dir != 0) {
            return;
        }
        row.value = row.value ? 0 : 1;
    } else if (row.kind == KIND_CHOICE && row.choice_count > 0 && (dir == 0 || row.arrows)) {
        int step = dir == 0 ? 1 : dir;
        row.value += step;
        if (row.value < 0) {
            row.value = row.choice_count - 1;
        }
        if (row.value >= row.choice_count) {
            row.value = 0;
        }
    } else if (row.kind == KIND_INT && (dir == 0 || row.arrows)) {
        int step = dir == 0 ? 1 : dir;
        row.value += step * row.step;
        if (row.value < row.min_v) {
            row.value = row.min_v;
        }
        if (row.value > row.max_v) {
            row.value = row.max_v;
        }
    } else {
        return;
    }
    refresh_setting_value(index);
}

static void onSettingsListItemClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    change_setting((int)(uintptr_t)userdata, 0);
}

static int setting_row_focused(paf::ui::Widget *item) {
    if (item == NULL) {
        return 0;
    }
    if (item->IsFocused()) {
        return 1;
    }
    paf::ui::Widget *button = item->FindChild("button");
    return button != NULL && button->IsFocused();
}

class SettingsPadListener : public paf::inputdevice::InputListener {
public:
    SettingsPadListener() : paf::inputdevice::InputListener(paf::inputdevice::DEVICE_TYPE_PAD) {}

    void OnUpdate(paf::inputdevice::Data *data) {
        if (data == NULL || data->m_pad_data == NULL) {
            g_prev_pad = 0;
            return;
        }

        uint32_t pad = data->m_pad_data->paddata;
        uint32_t pressed = pad & ~g_prev_pad;
        g_prev_pad = pad;

        if (page_is_open("page_settings_bubble")) {
            if (pressed & paf::inputdevice::pad::Data::PAD_ESCAPE) {
                close_settings_balloon();
            }
            return;
        }

        if (!page_is_open("page_settings")) {
            return;
        }

        int focused = -1;
        for (int i = 0; i < kSettingRowCount; i++) {
            if (setting_row_focused(g_row_widgets[i])) {
                focused = i;
                break;
            }
        }
        if (focused != g_focused_row) {
            g_focused_row = focused;
        }

        if (focused < 0) {
            return;
        }
        if (pressed & paf::inputdevice::pad::Data::PAD_LEFT) {
            change_setting(focused, -1);
        } else if (pressed & paf::inputdevice::pad::Data::PAD_RIGHT) {
            change_setting(focused, 1);
        }
    }
};

void SettingsItemFactory::Stop(StopParam& param) {
    for (int i = 0; i < kSettingRowCount; i++) {
        if (g_row_widgets[i] == param.list_item) {
            g_row_widgets[i] = NULL;
        }
    }
    param.list_item->Hide(paf::common::transition::Type_FadeinSlow);
}

paf::ui::ListItem *SettingsItemFactory::Create(CreateParam& param) {
    init_setting_rows();
    if (param.cell_index < 0 || param.cell_index >= kSettingRowCount) {
        return NULL;
    }

    const SettingRow& row = g_rows[param.cell_index];
    const char *template_id = "template_settings_row";
    if (row.kind == KIND_CATEGORY) {
        template_id = "template_settings_category";
    } else if (row.kind == KIND_NOTE) {
        template_id = "template_settings_note";
    }

    paf::Plugin::TemplateOpenParam openParam;
    int res = g_plugin->TemplateOpen(param.parent, template_id, openParam);
    if (res != 0) {
        return NULL;
    }

    paf::ui::ListItem *list_item = (paf::ui::ListItem *)param.parent->GetChild(param.parent->GetChildrenNum() - 1);
    if (list_item == NULL) {
        return NULL;
    }

    g_row_widgets[param.cell_index] = list_item;

    paf::ui::Widget *label = list_item->FindChild("label");
    if (label != NULL && row.label != NULL) {
        label->SetString(row.label);
    }

    paf::ui::Widget *value = list_item->FindChild("value");
    if (value != NULL) {
        wchar_t text[64];
        format_setting_value(row, text, 64);
        value->SetString(text);
    }

    paf::ui::Widget *button = list_item->FindChild("button");
    if (button != NULL) {
        button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSettingsListItemClick, (void *)(uintptr_t)param.cell_index);
    }

    return list_item;
}

static void clear_setting_widgets() {
    for (int i = 0; i < kSettingRowCount; i++) {
        g_row_widgets[i] = NULL;
    }
    g_settings_list = NULL;
    g_focused_row = -1;
    g_prev_pad = 0;
}

static void setup_settings_page(paf::ui::Scene *scene) {
    if (scene == NULL) {
        return;
    }

    init_setting_rows();
    clear_setting_widgets();

    paf::ui::ListView *settings_list_view = (paf::ui::ListView *)scene->FindChild("settings_list_view");
    if (settings_list_view) {
        g_settings_list = settings_list_view;
        settings_list_view->SetItemFactory(new SettingsItemFactory());
        settings_list_view->InsertSegment(0, 1);
        settings_list_view->SetCellSizeDefault(0, { 880.0f, 70.0f, 0.0f, 0.0f });
        settings_list_view->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
        settings_list_view->InsertCell(0, 0, kSettingRowCount);
        g_focused_row = 1;
        settings_list_view->SetFocus(0, 1, paf::ui::ListView::FOCUS_ALIGN_TYPE_HEAD, NULL);
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

    set_main_buttons_focusable(false);
    set_widget_focusable(scene->FindChild("btn_dismiss_balloon"), false);

    paf::ui::Widget *settings_button = scene->FindChild("btn_settings_balloon");
    if (settings_button != NULL) {
        settings_button->SetFocusedState(true);
    }
}

static void onDismissBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    close_settings_balloon();
}

static void onSpeechBalloonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    close_page("page_settings_bubble", paf::Plugin::TransitionType_None);
    paf::ui::Scene *scene = open_page("page_settings", paf::Plugin::TransitionType_SlideFromBottom);
    setup_settings_page(scene);
}

static void onCloseSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    clear_setting_widgets();
    close_page("page_settings", paf::Plugin::TransitionType_SlideFromBottom);
    set_main_buttons_focusable(true);
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

    paf::common::SharedPtr<paf::inputdevice::InputListener> settings_pad_listener(new SettingsPadListener());
    paf::inputdevice::AddInputListener(settings_pad_listener);

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
        g_main_scene = scene;
        if (scene != NULL) {
            bind_decide(scene, "btn_search_pcs", onSearchPCsButtonClick);
            bind_decide(scene, "btn_add_manually", onAddManuallyButtonClick);
            bind_decide(scene, "settings_button", onSettingsButtonClick);
        }
    }

    paf_fw->Run();
    return 0;
}
