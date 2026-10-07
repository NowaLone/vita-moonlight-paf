#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

namespace {

/*
 * Every balloon button takes a 72 px slot (60 px button + 6 px margins), which
 * gives the 72 px balloon used for Settings alone and 216 px for three buttons.
 */
static const float kBalloonButtonSlot = 72.0f;
static const int kBalloonButtonCount = 3;

/*
 * Distance from the screen's bottom edge to the balloon's bottom edge. The
 * system Photos balloon ends about 56 px above the screen bottom, just over
 * the "..." corner button.
 */
static const float kBalloonBottomOffset = 56.0f;

}

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

    paf::ui::Widget *bubble = root->FindChild("settings_speech_balloon");
    if (bubble) {
        const float width = 202.0f;
        const float parent_width = width + 12.0f;
        const float parent_height = kBalloonButtonSlot * kBalloonButtonCount;

        bubble->SetSize({parent_width, parent_height, 0, 0}, NULL);
        bubble->SetPos(
            264.0f + ((width - parent_width) / 2.0f),
            kBalloonBottomOffset,
            0,
            NULL
        );
        bubble->Show(paf::common::transition::Type_Popup4, 0.0f);
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
    if (s_instance == this) {
        s_instance = NULL;
    }
}
