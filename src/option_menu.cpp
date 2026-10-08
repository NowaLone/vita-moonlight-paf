#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

namespace {

/*
 * Every balloon button takes a 72 px slot (60 px button + 6 px margins), which
 * gives the 72 px balloon used for Settings alone and 216 px for three buttons.
 */
static const float kMinButtonWidth = 202.0f;
static const int kButtonCount = 3;

}

OptionMenu *OptionMenu::Instance()
{
    return s_instance;
}

void OptionMenu::OnSizeAdjust(int32_t type, paf::ui::Handler *self,
                              paf::ui::Event *event, void *userdata)
{
    (void)type;
    (void)event;

    paf::ui::Text *ruler = static_cast<paf::ui::Text *>(self);
    OptionMenu *menu = (OptionMenu *)userdata;
    if (!ruler || !menu || !menu->root) return;

    paf::ui::Widget *draw_obj = ruler->GetDrawObj(paf::ui::Text::OBJ_ROOT);
    if (!draw_obj) return;

    float width = draw_obj->GetSize().extract_x() + 40.0f;
    if (width < kMinButtonWidth) {
        width = kMinButtonWidth;
    }

    const float parent_width = width + 12.0f;
    const float parent_height = 12.0f + 60.0f * kButtonCount;
    const float parent_x = 264.0f + ((kMinButtonWidth - parent_width) / 2.0f);
    const float parent_y = 43.0f;

    const char *button_ids[kButtonCount] = {
        "btn_settings_balloon",
        "btn_copy_balloon",
        "btn_delete_balloon"
    };

    for (int i = 0; i < kButtonCount; ++i) {
        paf::ui::Widget *button = menu->root->FindChild(button_ids[i]);
        if (!button) continue;

        button->SetAdjust(
            paf::ui::Widget::ADJUST_NONE,
            paf::ui::Widget::ADJUST_NONE,
            paf::ui::Widget::ADJUST_NONE
        );
        button->SetSize({width, 60.0f}, NULL);
    }

    paf::ui::Widget *bubble = menu->root->FindChild("settings_speech_balloon");
    if (bubble) {
        bubble->SetSize({parent_width, parent_height, 0, 0}, NULL);
        bubble->SetPos(parent_x, parent_y, 0, NULL);
        bubble->Show(paf::common::transition::Type_Popup4, 0.0f);
    }

    ruler->DeleteEventCallback(
        paf::ui::Handler::CB_STATE_READY,
        OnSizeAdjust,
        userdata
    );
}

void OptionMenu::OnDismiss(int32_t type, paf::ui::Handler *self,
                           paf::ui::Event *event, void *userdata)
{
    (void)type;
    (void)self;
    (void)event;

    OptionMenu *menu = (OptionMenu *)userdata;
    if (!menu) return;

    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;

    if (callback) {
        callback(Event_Close, -1, data);
    }
}

void OptionMenu::OnSettings(int32_t type, paf::ui::Handler *self,
                            paf::ui::Event *event, void *userdata)
{
    OptionMenu *menu = (OptionMenu *)userdata;
    (void)type;
    (void)self;
    (void)event;

    if (!menu) return;

    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;

    if (callback) {
        callback(Event_Button, Button_Settings, data);
    }
}

void OptionMenu::OnCopy(int32_t type, paf::ui::Handler *self,
                        paf::ui::Event *event, void *userdata)
{
    OptionMenu *menu = (OptionMenu *)userdata;
    (void)type;
    (void)self;
    (void)event;

    if (!menu) return;

    if (menu->root) {
        menu->root->Hide(paf::common::transition::Type_Reset);
    }

    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;

    if (callback) {
        callback(Event_Button, Button_Copy, data);
    }
}

void OptionMenu::OnDelete(int32_t type, paf::ui::Handler *self,
                          paf::ui::Event *event, void *userdata)
{
    OptionMenu *menu = (OptionMenu *)userdata;
    (void)type;
    (void)self;
    (void)event;

    if (!menu) return;

    if (menu->root) {
        menu->root->Hide(paf::common::transition::Type_Reset);
    }

    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;

    if (callback) {
        callback(Event_Button, Button_Delete, data);
    }
}

OptionMenu::OptionMenu(paf::Plugin *plugin, paf::ui::Widget *parent,
                       EventCb cb, void *userdata, bool host_actions_enabled)
    : page::Base("page_settings_bubble", NULL,
                 paf::Plugin::TransitionType_None,
                 paf::Plugin::TransitionType_None),
      m_cb(cb),
      m_userdata(userdata)
{
    (void)plugin;
    (void)parent;

    if (!IsValid()) return;

    s_instance = this;

    m_close_param.fade = true;
    m_close_param.fade_time_ms = 1000.0f;

    paf::ui::Widget *settings_button = root->FindChild("btn_settings_balloon");
    paf::ui::Widget *copy_button = root->FindChild("btn_copy_balloon");
    paf::ui::Widget *delete_button = root->FindChild("btn_delete_balloon");
    paf::ui::Widget *dismiss_button = root->FindChild("btn_dismiss_balloon");
    paf::ui::Text *ruler = static_cast<paf::ui::Text *>(
        root->FindChild("text_option_menu_ruler")
    );

    paf::wstring longest_label;
    paf::ui::Widget *buttons[kButtonCount] = {
        settings_button,
        copy_button,
        delete_button
    };

    for (int i = 0; i < kButtonCount; ++i) {
        if (!buttons[i]) continue;

        paf::wstring label;
        if (buttons[i]->GetString(label) >= 0 &&
            label.length() > longest_label.length()) {
            longest_label = label;
        }
    }

    if (ruler && !longest_label.empty()) {
        ruler->SetString(longest_label);
        ruler->AddEventCallback(
            paf::ui::Handler::CB_STATE_READY,
            OnSizeAdjust,
            this
        );
    }

    if (dismiss_button) {
        dismiss_button->SetKeycode(
            paf::inputdevice::pad::Data::PAD_ESCAPE |
            paf::inputdevice::pad::Data::PAD_MENU
        );
    }

    bind_decide(root, "btn_settings_balloon", OnSettings, this);
    bind_decide(root, "btn_copy_balloon", OnCopy, this);
    bind_decide(root, "btn_delete_balloon", OnDelete, this);
    bind_decide(root, "btn_dismiss_balloon", OnDismiss, this);

    if (!host_actions_enabled) {
        paf::ui::Widget *copy_button = root->FindChild("btn_copy_balloon");
        paf::ui::Widget *delete_button = root->FindChild("btn_delete_balloon");
        if (copy_button) copy_button->SetActivate(false);
        if (delete_button) delete_button->SetActivate(false);
    }

    set_widget_focusable(
        root->FindChild("btn_dismiss_balloon"),
        false
    );

    paf::ui::Widget *settings_button = root->FindChild("btn_settings_balloon");
    if (settings_button) {
        settings_button->SetFocusedState(true);
    }
}

OptionMenu::~OptionMenu()
{
    if (root) {
        paf::ui::Widget *bubble = root->FindChild("settings_speech_balloon");
        if (bubble) {
            bubble->Hide(paf::common::transition::Type_Popup4, 0.0f);
        }
    }

    if (s_instance == this) {
        s_instance = NULL;
    }
}
