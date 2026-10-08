#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

namespace {

static const float kMinButtonWidth = 202.0f;
static const float kMaxButtonWidth = 700.0f;
static const int kButtonCount = 4;

class SpeechBalloonTestPage : public page::Base
{
public:
    SpeechBalloonTestPage()
        : page::Base("page_speech_balloon_test", NULL,
                     paf::Plugin::TransitionType_None,
                     paf::Plugin::TransitionType_None)
    {
        if (!IsValid()) return;

        /* Sony common_resource: _common_template_speech_balloon_menu_3. */
        paf::Plugin::TemplateOpenParam tmp_param;
        g_plugin->TemplateOpen(root, 0x4a34c804, tmp_param);

        paf::ui::Widget *button1 = root->FindChild(0x1bf75844);
        paf::ui::Widget *button2 = root->FindChild(0x6bbaa322);
        paf::ui::Widget *button3 = root->FindChild(0x21ca021a);

        if (button1) button1->SetString(g_plugin->GetString("msg_settings"));
        if (button2) button2->SetString(g_plugin->GetString("msg_copy"));
        if (button3) button3->SetString(g_plugin->GetString("msg_delete"));

        paf::ui::Widget *balloon = root->FindChild(0x2627246e);
        if (balloon) {
            /*
             * The Sony template has layout_hint adjust="2, 2, 0", so its
             * own layout would overwrite an explicit position. Disable the
             * adjustment first and then place the generated balloon.
             */
            balloon->SetAdjust(0, 0, 0);
            balloon->SetPosCenter(480.0f, 272.0f, 0.0f);
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

void OptionMenu::OnSizeAdjust(int32_t type, paf::ui::Handler *self,
                              paf::ui::Event *event, void *userdata)
{
    (void)type;
    (void)event;

    paf::ui::Text *ruler = static_cast<paf::ui::Text *>(self);
    OptionMenu *menu = (OptionMenu *)userdata;
    if (!ruler || !menu || !menu->root) return;

    paf::graph::DrawObj *draw_obj = ruler->GetDrawObj(paf::ui::Text::OBJ_ROOT);
    if (!draw_obj) return;

    float width = draw_obj->GetSize().extract_x() + 40.0f;
    if (width < kMinButtonWidth) {
        width = kMinButtonWidth;
    }
    if (width > kMaxButtonWidth) {
        width = kMaxButtonWidth;
    }

    const float parent_width = width + 12.0f;
    const float parent_height = 12.0f + 60.0f * kButtonCount;
    const float parent_x = 264.0f + ((kMinButtonWidth - parent_width) / 2.0f);
    const float parent_y = 43.0f;

    const char *button_ids[kButtonCount] = {
        "btn_settings_balloon",
        "btn_copy_balloon",
        "btn_delete_balloon",
        "btn_test_speech_balloon"
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
     * Static CXML nodes can already be in the READY state when this
     * constructor runs, so CB_STATE_READY on the ruler is not guaranteed to
     * fire again. Show the NetStream-sized fallback immediately; the ruler
     * callback may refine the width later when it is delivered.
     */
    paf::ui::Widget *bubble = root->FindChild("settings_speech_balloon");
    if (bubble) {
        const float width = kMinButtonWidth;
        const float parent_width = width + 12.0f;
        const float parent_height = 12.0f + 60.0f * kButtonCount;

        bubble->SetSize({parent_width, parent_height, 0, 0}, NULL);
        bubble->SetPos(
            264.0f + ((kMinButtonWidth - parent_width) / 2.0f),
            43.0f,
            0,
            NULL
        );
        bubble->Show(paf::common::transition::Type_Popup4, 0.0f);
    }

    m_close_param.fade = true;
    m_close_param.fade_time_ms = 1000.0f;

    paf::ui::Widget *settings_button = root->FindChild("btn_settings_balloon");
    paf::ui::Widget *copy_button = root->FindChild("btn_copy_balloon");
    paf::ui::Widget *delete_button = root->FindChild("btn_delete_balloon");
    paf::ui::Widget *test_speech_button = root->FindChild("btn_test_speech_balloon");
    paf::ui::Widget *dismiss_button = root->FindChild("btn_dismiss_balloon");
    paf::ui::Text *ruler = static_cast<paf::ui::Text *>(
        root->FindChild("text_option_menu_ruler")
    );

    paf::wstring longest_label;
    paf::ui::Widget *buttons[kButtonCount] = {
        settings_button,
        copy_button,
        delete_button,
        test_speech_button
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
