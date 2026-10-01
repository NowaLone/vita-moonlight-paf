#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

OptionMenu *OptionMenu::Instance()
{
    return s_instance;
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
    (void)type;
    (void)self;
    (void)event;

    OptionMenu *menu = (OptionMenu *)userdata;
    if (!menu) return;

    if (menu->root) {
        menu->root->Hide(paf::common::transition::Type_Reset);
    }

    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;

    if (callback) {
        callback(Event_Button, 0, data);
    }
}

OptionMenu::OptionMenu(paf::Plugin *plugin, paf::ui::Widget *parent,
                       EventCb cb, void *userdata)
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

    paf::ui::Widget *bubble = root->FindChild("settings_speech_balloon");
    if (bubble) {
        const float width = 202.0f;
        const float parent_width = width + 12.0f;
        const float parent_height = 72.0f;

        bubble->SetSize({parent_width, parent_height, 0, 0}, NULL);
        bubble->SetPos(
            264.0f + ((width - parent_width) / 2.0f),
            43.0f,
            0,
            NULL
        );
        bubble->Show(paf::common::transition::Type_Popup4, 0.0f);
    }

    bind_decide(root, "btn_settings_balloon", OnSettings, this);
    bind_decide(root, "btn_dismiss_balloon", OnDismiss, this);

    paf::ui::Widget *dismiss_button = root->FindChild("btn_dismiss_balloon");
    if (dismiss_button) {
        dismiss_button->SetEventCallback(
            paf::ui::ButtonBase::CB_BTN_CANCEL,
            OnDismiss,
            this
        );
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
    if (s_instance == this) {
        s_instance = NULL;
    }
}
