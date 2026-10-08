#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

namespace {

class SpeechBalloonTestPage : public page::Base
{
public:
    SpeechBalloonTestPage()
        : page::Base("page_speech_balloon_test", NULL,
                     paf::Plugin::TransitionType_None,
                     paf::Plugin::TransitionType_None)
    {
        if (!IsValid()) return;

        paf::ui::Widget *balloon = root->FindChild("test_speech_balloon");
        if (balloon) {
            balloon->Show(paf::common::transition::Type_Popup4, 0.0f);
        }

        bind_decide(root, "btn_close_speech_balloon_test",
                    page::Base::DefaultBackButtonCB, this);
    }

    virtual page::Type GetType()
    {
        return page::Type_SpeechBalloonTest;
    }
};

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

    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;

    if (callback) {
        callback(Event_Button, Button_Copy, data);
    }
}

void OptionMenu::OnSpeechBalloonTest(int32_t type, paf::ui::Handler *self,
                                     paf::ui::Event *event, void *userdata)
{
    (void)type;
    (void)self;
    (void)event;

    OptionMenu *menu = (OptionMenu *)userdata;
    if (!menu) return;

    delete menu;
    new SpeechBalloonTestPage();
}

void OptionMenu::OnDelete(int32_t type, paf::ui::Handler *self,
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

    /*
     * Let the speech_balloon and its vertical Box determine their own size.
     * Keep positioning as a separate concern; (0,0) is the verified PAF
     * center-origin position for this widget.
     */
    paf::ui::Widget *bubble = root->FindChild("settings_speech_balloon");
    if (bubble) {
        bubble->SetPosCenter(paf::math::v4(264.0f, 43.0f, 0.0f));
        bubble->Show(paf::common::transition::Type_Popup4, 0.0f);
    }

    m_close_param.fade = true;
    m_close_param.fade_time_ms = 1000.0f;

    paf::ui::Widget *settings_button = root->FindChild("btn_settings_balloon");
    paf::ui::Widget *copy_button = root->FindChild("btn_copy_balloon");
    paf::ui::Widget *delete_button = root->FindChild("btn_delete_balloon");
    paf::ui::Widget *test_speech_button = root->FindChild("btn_test_speech_balloon");
    paf::ui::Widget *dismiss_button = root->FindChild("btn_dismiss_balloon");
    if (dismiss_button) {
        dismiss_button->SetKeycode(
            paf::inputdevice::pad::Data::PAD_ESCAPE |
            paf::inputdevice::pad::Data::PAD_MENU
        );
    }

    bind_decide(root, "btn_settings_balloon", OnSettings, this);
    bind_decide(root, "btn_copy_balloon", OnCopy, this);
    bind_decide(root, "btn_delete_balloon", OnDelete, this);
    bind_decide(root, "btn_test_speech_balloon", OnSpeechBalloonTest, this);
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
