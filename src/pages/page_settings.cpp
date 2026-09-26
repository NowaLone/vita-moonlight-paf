#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <stdio.h>

#include "pages/page_settings.h"
#include "settings_model.h"
#include "option_menu.h"
#include "common.h"
#include "moonlight/api.h"

namespace page {

static paf::ui::Widget *g_row_widgets[kSettingRowMax];
static paf::ui::Widget *g_section_menu_items[kSettingSectionMax];
static int g_open_section = -1;
static int g_focused_row = -1;
static uint32_t g_prev_pad = 0;
static int g_back_delay = 0;
static int g_back_target = 0;
static int g_picker_circle = 0;
static int g_back_wait_release = 0;

static const int kMaxPickerOptions = 40;
static int g_picker_row = -1;
static int g_picker_count = 0;
static int g_picker_current = 0;
static int g_picker_values[kMaxPickerOptions];
static wchar_t g_picker_text[kMaxPickerOptions][16];
static const wchar_t *g_picker_labels[kMaxPickerOptions];
static paf::ui::Widget *g_picker_items[kMaxPickerOptions];

static SettingsRoot *s_settings_root = NULL;

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

static void clear_setting_widgets() {
    for (int i = 0; i < kSettingRowMax; i++) {
        g_row_widgets[i] = NULL;
    }
    g_focused_row = -1;
    g_prev_pad = 0;
}

static void fit_list_under_header(paf::ui::ListView *list, int count) {
    if (list == NULL) {
        return;
    }
    const float cell_h = 82.0f;
    const float max_h = 448.0f;
    float height = (float)count * cell_h;
    if (height < cell_h) {
        height = cell_h;
    }
    if (height > max_h) {
        height = max_h;
    }
    list->SetSize({ 900.0f, height, 0.0f, 0.0f }, NULL);
}

static void set_setting_texts(paf::ui::Widget *item, const SettingRow& row) {
    if (item == NULL) {
        return;
    }
    paf::ui::Widget *button = item->FindChild("button");
    if (button != NULL && row.label != NULL) {
        button->SetString(row.label);
    }
    paf::ui::Widget *value = item->FindChild("value");
    if (value == NULL) {
        return;
    }
    set_widget_focusable(value, false);
    if (row.kind == KIND_CHOICE || row.kind == KIND_INT || row.kind == KIND_FIXED) {
        wchar_t text[64];
        settings_model_format_value(row, text, 64);
        value->SetString(text);
    }
}

static void sync_toggle_box(paf::ui::Widget *item, int on) {
    if (item == NULL) {
        return;
    }
    paf::ui::CheckBox *box = (paf::ui::CheckBox *)item->FindChild("check");
    if (box != NULL) {
        box->SetCheck(on != 0, false);
    }
}

static void refresh_setting_value(int index) {
    SettingRow *row = settings_model_row(index);
    if (row == NULL || g_row_widgets[index] == NULL) {
        return;
    }
    set_setting_texts(g_row_widgets[index], *row);
    if (row->kind == KIND_TOGGLE) {
        sync_toggle_box(g_row_widgets[index], row->value);
    }
}

void SetSettingsMenuFocusable(bool on) {
    Base *root_page = Base::Find("page_settings");
    if (root_page != NULL && root_page->root != NULL) {
        set_widget_focusable(root_page->root->FindChild("btn_back_settings"), on);
        set_widget_focusable(root_page->root->FindChild("settings_list_view"), on);
    }
    int section_count = settings_model_section_count();
    for (int i = 0; i < section_count; i++) {
        paf::ui::Widget *item = g_section_menu_items[i];
        set_widget_focusable(item, on);
        if (item != NULL) {
            set_widget_focusable(item->FindChild("button"), on);
        }
    }
}

void SetSectionRowsFocusable(bool on) {
    Base *section_page = Base::Find("page_settings_section");
    if (section_page != NULL && section_page->root != NULL) {
        set_widget_focusable(section_page->root->FindChild("btn_back_section"), on);
        set_widget_focusable(section_page->root->FindChild("section_list_view"), on);
    }
    int row_count = settings_model_row_count();
    for (int i = 0; i < row_count; i++) {
        paf::ui::Widget *item = g_row_widgets[i];
        if (item == NULL) {
            continue;
        }
        set_widget_focusable(item, on);
        paf::ui::Widget *check = item->FindChild("check");
        if (check != NULL) {
            set_widget_focusable(item->FindChild("button"), false);
            set_widget_focusable(check, on);
        } else {
            set_widget_focusable(item->FindChild("button"), on);
        }
    }
}

void SetSettingsBackActive(bool on) {
    Base *section_page = Base::Find("page_settings_section");
    Base *root_page = Base::Find("page_settings");
    paf::ui::Widget *backs[2];
    backs[0] = section_page != NULL && section_page->root != NULL ? section_page->root->FindChild("btn_back_section") : NULL;
    backs[1] = root_page != NULL && root_page->root != NULL ? root_page->root->FindChild("btn_back_settings") : NULL;
    for (int i = 0; i < 2; i++) {
        if (backs[i] == NULL) {
            continue;
        }
        paf::ui::ButtonBase *button = (paf::ui::ButtonBase *)backs[i];
        paf::Timer *timer = new paf::Timer(200.0f, paf::Timer::Func::FUNC_LINEAR);
        if (on) {
            button->Enable(true);
            button->SetMetaAlpha(1.0f, timer);
        } else {
            button->SetDisableColor(1.0f, 1.0f, 1.0f, 1.0f);
            button->Disable(true);
            button->SetMetaAlpha(0.35f, timer);
        }
    }
}

void CloseChoicePicker(int from_circle) {
    if (!Base::IsOpen("page_choice_picker")) {
        g_picker_row = -1;
        return;
    }
    for (int i = 0; i < kMaxPickerOptions; i++) {
        g_picker_items[i] = NULL;
    }
    g_picker_row = -1;
    Base::CloseType(Type_ChoicePicker);
    if (from_circle) {
        g_back_wait_release = 1;
    } else {
        SetSettingsBackActive(true);
    }
    SetSectionRowsFocusable(true);
}

static int fill_picker_options(int row_index) {
    SettingRow *row = settings_model_row(row_index);
    if (row == NULL) {
        return 0;
    }
    g_picker_count = 0;
    g_picker_current = 0;
    if (row->kind == KIND_CHOICE && row->choices != NULL) {
        int count = row->choice_count;
        if (count > kMaxPickerOptions) {
            count = kMaxPickerOptions;
        }
        for (int i = 0; i < count; i++) {
            g_picker_labels[i] = row->choices[i] != NULL ? row->choices[i] : L"";
            g_picker_values[i] = i;
        }
        g_picker_count = count;
        if (row->value >= 0 && row->value < count) {
            g_picker_current = row->value;
        }
        return g_picker_count > 0;
    }
    if (row->kind != KIND_INT) {
        return 0;
    }
    int step = row->step > 0 ? row->step : 1;
    for (int value = row->min_v; value <= row->max_v && g_picker_count < kMaxPickerOptions; value += step) {
        int n = g_picker_count;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);
        ascii_to_wide(g_picker_text[n], 16, buf);
        g_picker_labels[n] = g_picker_text[n];
        g_picker_values[n] = value;
        if (value == row->value) {
            g_picker_current = n;
        }
        g_picker_count++;
        if (step > 0 && value > row->max_v - step) {
            break;
        }
    }
    return g_picker_count > 0;
}

void OpenChoicePicker(int row_index) {
    if (row_index < 0 || row_index >= settings_model_row_count() || Base::IsOpen("page_choice_picker")) {
        return;
    }
    if (!fill_picker_options(row_index)) {
        return;
    }
    new ChoicePicker(row_index);
}

void CloseSettingsSection() {
    if (Base::IsOpen("page_choice_picker")) {
        CloseChoicePicker();
    }
    if (!Base::IsOpen("page_settings_section")) {
        return;
    }
    clear_setting_widgets();
    g_open_section = -1;
    Base::CloseType(Type_SettingsSection);
    SetSettingsMenuFocusable(true);
}

void CloseSettingsRoot() {
    if (Base::IsOpen("page_settings_section")) {
        CloseSettingsSection();
    }
    clear_setting_widgets();
    g_open_section = -1;
    Base::CloseType(Type_SettingsRoot);
    SetMainButtonsFocusable(true);
    moonlight_api_apply_settings();
}

static void onSettingsListItemClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    int index = (int)(uintptr_t)userdata;
    SettingRow *row = settings_model_row(index);
    if (row == NULL) {
        return;
    }
    if (row->kind == KIND_CHOICE || row->kind == KIND_INT) {
        OpenChoicePicker(index);
        return;
    }
    if (row->kind == KIND_TOGGLE) {
        row->value = row->value ? 0 : 1;
        refresh_setting_value(index);
    }
}

static void onToggleCheckClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)e;
    int index = (int)(uintptr_t)userdata;
    paf::ui::CheckBox *box = (paf::ui::CheckBox *)self;
    SettingRow *row = settings_model_row(index);
    if (row == NULL || box == NULL) {
        return;
    }
    row->value = box->IsChecked() ? 1 : 0;
}

static void onPickerDismiss(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    CloseChoicePicker();
}

static void onPickerItemClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    int option = (int)(uintptr_t)userdata;
    if (g_picker_row < 0 || option < 0 || option >= g_picker_count) {
        CloseChoicePicker();
        return;
    }
    SettingRow *row = settings_model_row(g_picker_row);
    if (row != NULL) {
        row->value = g_picker_values[option];
        refresh_setting_value(g_picker_row);
    }
    CloseChoicePicker();
}

class SettingsItemFactory : public paf::ui::listview::ItemFactory {
public:
    paf::ui::ListItem *Create(CreateParam& param);
    void Start(StartParam& param) {
        param.list_item->Show(paf::common::transition::Type_Reset);
    }
    void Stop(StopParam& param);
};

class SectionMenuFactory : public paf::ui::listview::ItemFactory {
public:
    paf::ui::ListItem *Create(CreateParam& param);
    void Start(StartParam& param) {
        param.list_item->Show(paf::common::transition::Type_Reset);
    }
    void Stop(StopParam& param);
};

class PickerItemFactory : public paf::ui::listview::ItemFactory {
public:
    paf::ui::ListItem *Create(CreateParam& param);
    void Start(StartParam& param) {
        param.list_item->Show(paf::common::transition::Type_Reset);
    }
    void Stop(StopParam& param);
};

void SettingsItemFactory::Stop(StopParam& param) {
    for (int i = 0; i < kSettingRowMax; i++) {
        if (g_row_widgets[i] == param.list_item) {
            g_row_widgets[i] = NULL;
        }
    }
    param.list_item->Hide(paf::common::transition::Type_Reset);
}

paf::ui::ListItem *SettingsItemFactory::Create(CreateParam& param) {
    SettingSection *section = settings_model_section(g_open_section);
    if (section == NULL || param.cell_index < 0 || param.cell_index >= section->count) {
        return NULL;
    }
    int row_index = section->first + param.cell_index;
    SettingRow *row = settings_model_row(row_index);
    if (row == NULL) {
        return NULL;
    }

    const char *template_id = "template_settings_row";
    if (row->kind == KIND_NOTE) {
        template_id = "template_settings_note";
    } else if (row->kind == KIND_TOGGLE) {
        template_id = "template_settings_toggle";
    }

    paf::Plugin::TemplateOpenParam openParam;
    if (g_plugin->TemplateOpen(param.parent, template_id, openParam) != 0) {
        return NULL;
    }

    paf::ui::ListItem *list_item = (paf::ui::ListItem *)param.parent->GetChild(param.parent->GetChildrenNum() - 1);
    if (list_item == NULL) {
        return NULL;
    }

    g_row_widgets[row_index] = list_item;
    set_setting_texts(list_item, *row);

    paf::ui::Widget *button = list_item->FindChild("button");
    if (button != NULL) {
        button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSettingsListItemClick, (void *)(uintptr_t)row_index);
    }

    if (row->kind == KIND_TOGGLE) {
        paf::ui::CheckBox *box = (paf::ui::CheckBox *)list_item->FindChild("check");
        sync_toggle_box(list_item, row->value);
        set_widget_focusable(button, false);
        if (box != NULL) {
            box->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onToggleCheckClick, (void *)(uintptr_t)row_index);
        }
    }

    return list_item;
}

void SectionMenuFactory::Stop(StopParam& param) {
    for (int i = 0; i < kSettingSectionMax; i++) {
        if (g_section_menu_items[i] == param.list_item) {
            g_section_menu_items[i] = NULL;
        }
    }
    param.list_item->Hide(paf::common::transition::Type_Reset);
}

static void onSectionMenuClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    int section_index = (int)(uintptr_t)userdata;
    if (settings_model_section(section_index) == NULL) {
        return;
    }
    if (Base::IsOpen("page_settings_section")) {
        return;
    }
    new SettingsSection(section_index);
}

paf::ui::ListItem *SectionMenuFactory::Create(CreateParam& param) {
    SettingSection *section = settings_model_section(param.cell_index);
    if (section == NULL) {
        return NULL;
    }

    paf::Plugin::TemplateOpenParam openParam;
    if (g_plugin->TemplateOpen(param.parent, "template_settings_section", openParam) != 0) {
        return NULL;
    }

    paf::ui::ListItem *list_item = (paf::ui::ListItem *)param.parent->GetChild(param.parent->GetChildrenNum() - 1);
    if (list_item == NULL) {
        return NULL;
    }

    g_section_menu_items[param.cell_index] = list_item;
    paf::ui::Widget *button = list_item->FindChild("button");
    if (button != NULL) {
        if (section->title != NULL) {
            button->SetString(section->title);
        }
        button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onSectionMenuClick, (void *)(uintptr_t)param.cell_index);
    }
    set_widget_focusable(list_item->FindChild("arrow"), false);
    return list_item;
}

void PickerItemFactory::Stop(StopParam& param) {
    for (int i = 0; i < kMaxPickerOptions; i++) {
        if (g_picker_items[i] == param.list_item) {
            g_picker_items[i] = NULL;
        }
    }
    param.list_item->Hide(paf::common::transition::Type_Reset);
}

paf::ui::ListItem *PickerItemFactory::Create(CreateParam& param) {
    if (param.cell_index < 0 || param.cell_index >= g_picker_count) {
        return NULL;
    }

    paf::Plugin::TemplateOpenParam openParam;
    if (g_plugin->TemplateOpen(param.parent, "template_picker_option", openParam) != 0) {
        return NULL;
    }

    paf::ui::ListItem *list_item = (paf::ui::ListItem *)param.parent->GetChild(param.parent->GetChildrenNum() - 1);
    if (list_item == NULL) {
        return NULL;
    }

    g_picker_items[param.cell_index] = list_item;
    paf::ui::Widget *button = list_item->FindChild("button");
    if (button != NULL && g_picker_labels[param.cell_index] != NULL) {
        button->SetString(g_picker_labels[param.cell_index]);
        button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, onPickerItemClick, (void *)(uintptr_t)param.cell_index);
    }

    paf::ui::Widget *mark = list_item->FindChild("mark");
    if (mark != NULL) {
        set_widget_focusable(mark, false);
        if (param.cell_index != g_picker_current) {
            mark->Hide(paf::common::transition::Type_Reset);
        }
    }
    return list_item;
}

static void onCloseSectionButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    if (g_picker_circle || Base::IsOpen("page_choice_picker")) {
        g_picker_circle = 1;
        CloseChoicePicker(1);
        return;
    }
    CloseSettingsSection();
}

static void onCloseSettingsButtonClick(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    if (g_picker_circle || Base::IsOpen("page_choice_picker")) {
        g_picker_circle = 1;
        CloseChoicePicker(1);
        return;
    }
    CloseSettingsRoot();
}

class SettingsPadListener : public paf::inputdevice::InputListener {
public:
    SettingsPadListener() : paf::inputdevice::InputListener(paf::inputdevice::DEVICE_TYPE_PAD) {}

    void OnUpdate(paf::inputdevice::Data *data) {
        if (g_back_delay > 0) {
            if (g_picker_circle) {
                g_back_delay = 0;
                g_back_target = 0;
                return;
            }
            g_back_delay--;
            if (g_back_delay == 0) {
                int target = g_back_target;
                g_back_target = 0;
                if (target == 1) {
                    CloseSettingsSection();
                } else if (target == 2) {
                    CloseSettingsRoot();
                }
            }
            return;
        }
        if (data == NULL || data->m_pad_data == NULL) {
            g_prev_pad = 0;
            return;
        }

        uint32_t pad = data->m_pad_data->paddata;
        uint32_t pressed = pad & ~g_prev_pad;
        g_prev_pad = pad;

        if (!(pad & paf::inputdevice::pad::Data::PAD_ESCAPE)) {
            g_picker_circle = 0;
            if (g_back_wait_release) {
                g_back_wait_release = 0;
                SetSettingsBackActive(true);
            }
        }

        if (OptionMenu::Instance() != NULL) {
            if (pressed & paf::inputdevice::pad::Data::PAD_ESCAPE) {
                delete OptionMenu::Instance();
            }
            return;
        }

        if (Base::IsOpen("page_choice_picker")) {
            if (pressed & paf::inputdevice::pad::Data::PAD_ESCAPE) {
                g_picker_circle = 1;
                CloseChoicePicker(1);
            }
            return;
        }

        if (g_picker_circle) {
            return;
        }

        if (Base::IsOpen("page_settings_section")) {
            if (pressed & paf::inputdevice::pad::Data::PAD_ESCAPE) {
                Base *section_page = Base::Find("page_settings_section");
                paf::ui::Widget *back = section_page != NULL && section_page->root != NULL
                    ? section_page->root->FindChild("btn_back_section") : NULL;
                if (back != NULL) {
                    ((paf::ui::ButtonBase *)back)->ChangeButtonState(paf::ui::ButtonBase::ST_BTN_PRESS);
                }
                g_back_target = 1;
                g_back_delay = 10;
            }
            return;
        }
        if (Base::IsOpen("page_settings")) {
            if (pressed & paf::inputdevice::pad::Data::PAD_ESCAPE) {
                Base *root_page = Base::Find("page_settings");
                paf::ui::Widget *back = root_page != NULL && root_page->root != NULL
                    ? root_page->root->FindChild("btn_back_settings") : NULL;
                if (back != NULL) {
                    ((paf::ui::ButtonBase *)back)->ChangeButtonState(paf::ui::ButtonBase::ST_BTN_PRESS);
                }
                g_back_target = 2;
                g_back_delay = 10;
            }
        }
    }
};

SettingsRoot::SettingsRoot()
    : Base("page_settings", "btn_back_settings",
           paf::Plugin::TransitionType_None, paf::Plugin::TransitionType_None) {
    s_settings_root = this;
    settings_model_init();
    clear_setting_widgets();
    g_open_section = -1;
    for (int i = 0; i < kSettingSectionMax; i++) {
        g_section_menu_items[i] = NULL;
    }

    paf::ui::ListView *settings_list_view = (paf::ui::ListView *)root->FindChild("settings_list_view");
    int section_count = settings_model_section_count();
    if (settings_list_view) {
        settings_list_view->SetItemFactory(new SectionMenuFactory());
        settings_list_view->InsertSegment(0, 1);
        settings_list_view->SetCellSizeDefault(0, { 900.0f, 82.0f, 0.0f, 0.0f });
        settings_list_view->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
        settings_list_view->InsertCell(0, 0, section_count);
        fit_list_under_header(settings_list_view, section_count);
        settings_list_view->SetFocus(0, 0, paf::ui::ListView::FOCUS_ALIGN_TYPE_HEAD, NULL);
    }

    bind_decide(root, "btn_back_settings", onCloseSettingsButtonClick);
    set_widget_focusable(root->FindChild("text_settings_title"), false);
}

SettingsRoot::~SettingsRoot() {
    if (s_settings_root == this) {
        s_settings_root = NULL;
    }
}

SettingsRoot *SettingsRoot::Instance() {
    return s_settings_root;
}

SettingsSection::SettingsSection(int section_index)
    : Base("page_settings_section", "btn_back_section",
           paf::Plugin::TransitionType_None, paf::Plugin::TransitionType_None),
      m_section_index(section_index) {
    g_open_section = section_index;
    clear_setting_widgets();

    SettingSection *section = settings_model_section(section_index);
    paf::ui::Widget *title = root->FindChild("text_section_title");
    if (title != NULL && section != NULL && section->title != NULL) {
        title->SetString(section->title);
    }

    paf::ui::ListView *section_list = (paf::ui::ListView *)root->FindChild("section_list_view");
    int focus_local = 0;
    if (section_list != NULL && section != NULL) {
        for (int i = 0; i < section->count; i++) {
            SettingRow *row = settings_model_row(section->first + i);
            if (row != NULL && row->kind != KIND_NOTE && row->kind != KIND_FIXED) {
                focus_local = i;
                break;
            }
        }
        section_list->SetItemFactory(new SettingsItemFactory());
        section_list->InsertSegment(0, 1);
        section_list->SetCellSizeDefault(0, { 900.0f, 82.0f, 0.0f, 0.0f });
        section_list->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
        section_list->InsertCell(0, 0, section->count);
        fit_list_under_header(section_list, section->count);
        g_focused_row = section->first + focus_local;
        section_list->SetFocus(0, 0, paf::ui::ListView::FOCUS_ALIGN_TYPE_HEAD, NULL);
    }

    bind_decide(root, "btn_back_section", onCloseSectionButtonClick);
    set_widget_focusable(root->FindChild("text_section_title"), false);
    SetSettingsMenuFocusable(false);
}

SettingsSection::~SettingsSection() {}

ChoicePicker::ChoicePicker(int row_index)
    : Base("page_choice_picker", NULL,
           paf::Plugin::TransitionType_None, paf::Plugin::TransitionType_None),
      m_row_index(row_index) {
    g_picker_row = row_index;
    paf::ui::ListView *list = (paf::ui::ListView *)root->FindChild("picker_list");
    if (list != NULL) {
        list->SetItemFactory(new PickerItemFactory());
        list->InsertSegment(0, 1);
        list->SetCellSizeDefault(0, { 400.0f, 64.0f, 0.0f, 0.0f });
        list->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
        list->InsertCell(0, 0, g_picker_count);
        float list_h = 448.0f;
        float content_h = (float)g_picker_count * 64.0f;
        if (content_h > list_h) {
            content_h = list_h;
        }
        list->SetPos(0.0f, -((list_h - content_h) * 0.5f), 0.0f, NULL);
        list->SetFocus(0, g_picker_current, paf::ui::ListView::FOCUS_ALIGN_TYPE_HEAD, NULL);
    }

    bind_decide(root, "btn_picker_dismiss", onPickerDismiss);
    set_widget_focusable(root->FindChild("btn_picker_dismiss"), false);
    SetSectionRowsFocusable(false);
    SetSettingsBackActive(false);
}

ChoicePicker::~ChoicePicker() {}

void SettingsPadInit() {
    static int ready = 0;
    static paf::common::SharedPtr<paf::inputdevice::InputListener> listener;
    if (ready) {
        return;
    }
    ready = 1;
    listener = paf::common::SharedPtr<paf::inputdevice::InputListener>(new SettingsPadListener());
    paf::inputdevice::AddInputListener(listener);
}

}
