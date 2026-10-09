#include "option_menu.h"
#include "common.h"

static OptionMenu *s_instance = NULL;

namespace {

/*
 * The popup uses PAF centre-origin coordinates (+Y up). Its auto-sized
 * vertical Box determines the balloon height; keep the bottom edge above the
 * bottom-right corner button and derive Y from the buttons currently present.
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

/*
 * Disabled option-menu label. 0.55/0.72/0.86 still read too dark on the black
 * button texture, so the label is full white. Set the color before Disable();
 * the shared helper writes 0.55 and then disables, and a later write does not
 * replace that. Do not Widget::SetColor the plate: it multiplies the black
 * texture.
 */
static const float kBrowserDisabledLabel = 1.0f;

static int count_balloon_buttons(paf::ui::Scene *scene)
{
    static const char *const ids[] = {
        "btn_settings_balloon",
        "btn_delete_balloon"
    };

    int count = 0;
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (scene->FindChild(ids[i]) != NULL) {
            ++count;
        }
    }
    return count;
}

static void style_balloon_delete_disabled(paf::ui::Widget *button)
{
    if (button == NULL) {
        return;
    }

    paf::ui::ButtonBase *button_base =
        static_cast<paf::ui::ButtonBase *>(button);
    button_base->SetDisableColor(
        kBrowserDisabledLabel,
        kBrowserDisabledLabel,
        kBrowserDisabledLabel,
        1.0f
    );
    button_base->Disable();
    button->SetActivate(false);
}

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
    paf::ui::Widget *delete_button = root->FindChild("btn_delete_balloon");
    paf::ui::Widget *dismiss_button = root->FindChild("btn_dismiss_balloon");
    if (dismiss_button) {
        dismiss_button->SetKeycode(
            paf::inputdevice::pad::Data::PAD_ESCAPE |
            paf::inputdevice::pad::Data::PAD_MENU
        );
    }

    bind_decide(root, "btn_settings_balloon", OnSettings, this);
    bind_decide(root, "btn_delete_balloon", OnDelete, this);
    bind_decide(root, "btn_dismiss_balloon", OnDismiss, this);

    if (!host_actions_enabled) {
        style_balloon_delete_disabled(delete_button);
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
