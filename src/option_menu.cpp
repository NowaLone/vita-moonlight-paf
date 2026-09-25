#include "option_menu.h"
#include "common.h"
#include "pages/page.h"

static OptionMenu *s_instance = NULL;

OptionMenu *OptionMenu::Instance() {
    return s_instance;
}

void OptionMenu::OnDismiss(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    OptionMenu *menu = (OptionMenu *)userdata;
    if (menu == NULL) {
        return;
    }
    OptionMenu::EventCb cb = menu->m_cb;
    void *cb_data = menu->m_userdata;
    delete menu;
    if (cb != NULL) {
        cb(Event_Close, -1, cb_data);
    }
}

void OptionMenu::OnSettings(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    OptionMenu *menu = (OptionMenu *)userdata;
    if (menu == NULL) {
        return;
    }
    OptionMenu::EventCb cb = menu->m_cb;
    void *cb_data = menu->m_userdata;
    delete menu;
    if (cb != NULL) {
        cb(Event_Button, 0, cb_data);
    }
}

OptionMenu::OptionMenu(paf::Plugin *plugin, paf::ui::Widget *parent, EventCb cb, void *userdata)
    : m_plugin(plugin), m_scene(NULL), m_parent(parent), m_cb(cb), m_userdata(userdata) {
    s_instance = this;
    if (m_plugin == NULL) {
        return;
    }

    paf::Plugin::PageOpenParam open_param = make_open_param(paf::Plugin::TransitionType_None);
    m_scene = m_plugin->PageOpen("page_settings_bubble", open_param);
    if (m_scene == NULL) {
        return;
    }

    if (m_parent != NULL) {
        m_parent->SetActivate(false);
    }
    page::SetMainButtonsFocusable(false);

    paf::ui::Widget *balloon = m_scene->FindChild("settings_speech_balloon");
    if (balloon != NULL) {
        const float button_w = 202.0f;
        const float plane_w = button_w + 12.0f;
        const float plane_h = 12.0f + 60.0f;
        balloon->SetSize({ plane_w, plane_h, 0.0f, 0.0f }, NULL);
        balloon->SetPos(264.0f + ((button_w - plane_w) / 2.0f), 43.0f, 0.0f, NULL);
        balloon->Show(paf::common::transition::Type_Popup4, 0.0f);
    }

    bind_decide(m_scene, "btn_settings_balloon", OnSettings, this);
    bind_decide(m_scene, "btn_dismiss_balloon", OnDismiss, this);
    set_widget_focusable(m_scene->FindChild("btn_dismiss_balloon"), false);

    paf::ui::Widget *settings_button = m_scene->FindChild("btn_settings_balloon");
    if (settings_button != NULL) {
        settings_button->SetFocusedState(true);
    }
}

OptionMenu::~OptionMenu() {
    if (s_instance == this) {
        s_instance = NULL;
    }
    if (m_plugin != NULL && m_scene != NULL) {
        m_plugin->PageClose("page_settings_bubble", make_close_param(paf::Plugin::TransitionType_None));
    }
    if (m_parent != NULL) {
        m_parent->SetActivate(true);
    }
    if (!page::Base::IsOpen("page_settings")) {
        page::SetMainButtonsFocusable(true);
    }
}
