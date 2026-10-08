#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

namespace {

/*
 * Placement of the generated speech_balloon. SetPos() on this widget takes the
 * balloon's centre in PAF centre-origin coordinates (+Y up): the previous
 * SetPos(420, -140) left the balloon's left edge at screen x ~795 and its top
 * at y ~287, i.e. a ~214x252 box (4 buttons) centred on that point.
 */

/* Every button is 60 px high; the box adds 6 px margin above and below. */
static const float kBalloonButtonHeight = 60.0f;
static const float kBalloonPadding = 12.0f;

/*
 * Horizontal centre. The balloon is 214 px wide (202 px buttons + 6 px margins
 * on both sides), so x = 258 spans screen x 631..845, left of the "..." corner
 * button, like the system Photos popup.
 */
static const float kBalloonCenterX = 258.0f;

/*
 * Like the system Photos popup, the balloon's bottom edge sits 50 px above the
 * screen bottom, directly over the "..." corner button.
 */
static const float kBalloonBottomMargin = 50.0f;
static const float kScreenHalfHeight = 272.0f;

static int count_balloon_buttons(paf::ui::Scene *scene)
{
    static const char *const ids[] = {
        "btn_settings_balloon",
        "btn_copy_balloon",
        "btn_delete_balloon",
        "btn_test_speech_balloon"
    };

    int count = 0;
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (scene->FindChild(ids[i]) != NULL) {
            ++count;
        }
    }
    return count;
}

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
     * The speech_balloon and its vertical Box determine their own size, so
     * derive the height from the buttons that are actually present and pin the
     * balloon's bottom edge instead of hard-coding its centre.
     */
    paf::ui::Widget *bubble = root->FindChild("settings_speech_balloon");
    if (bubble) {
        const float height =
            kBalloonButtonHeight * count_balloon_buttons(root) + kBalloonPadding;
        const float center_y =
            kBalloonBottomMargin + height / 2.0f - kScreenHalfHeight;

        bubble->SetPos(paf::math::v4(kBalloonCenterX, center_y, 0.0f));
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
        set_button_enabled(copy_button, false);
        set_button_enabled(delete_button, false);
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
