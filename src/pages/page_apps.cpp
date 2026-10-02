#include <paf.h>
#include <string.h>

#include "pages/page_apps.h"
#include "app/moonlight_app.h"

namespace page {

namespace {
static const int kMaxApps = 8;

static const char *AppButtonId(int index) {
    static const char *ids[kMaxApps] = {
        "btn_app_0", "btn_app_1", "btn_app_2", "btn_app_3",
        "btn_app_4", "btn_app_5", "btn_app_6", "btn_app_7"
    };
    return (index >= 0 && index < kMaxApps) ? ids[index] : NULL;
}
}

Apps::Apps()
    : Base("page_apps", "btn_close_apps",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom),
      m_app_count(0),
      m_launching(false) {
    if (!IsValid()) return;

    for (int i = 0; i < kMaxApps; ++i) {
        memset(&m_apps[i], 0, sizeof(m_apps[i]));
        m_button_contexts[i].page = this;
        m_button_contexts[i].index = i;
        paf::ui::Widget *button = root->FindChild(AppButtonId(i));
        if (button) {
            button->SetEventCallback(
                paf::ui::ButtonBase::CB_BTN_DECIDE,
                OnAppButton,
                &m_button_contexts[i]);
            button->Hide(paf::common::transition::Type_Reset);
        }
    }

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    paf::ui::Widget *title = root->FindChild("text_apps_title");
    if (title) {
        title->SetString(paf::common::string_util::ToWString("Apps"));
    }
    SetStatus("Loading apps...");
    paf::ui::Widget *loading = root->FindChild("btn_app_0");
    if (loading) {
        loading->SetString(paf::common::string_util::ToWString("LOADING APPS..."));
        loading->Show(paf::common::transition::Type_Reset);
    }

    int result = MoonlightApp::Instance()->Applications().Refresh();
    if (result != 0) {
        SetStatus("App list start failed");
    }
}

Apps::~Apps() {
    if (MoonlightApp::Instance() && MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
    }
}

void Apps::OnAppButton(int32_t type,
                       paf::ui::Handler *self,
                       paf::ui::Event *event,
                       void *userdata) {
    (void)type;
    (void)self;
    (void)event;

    AppButtonContext *context = (AppButtonContext *)userdata;
    if (!context || !context->page) return;
    context->page->SelectApp(context->index);
}

void Apps::SelectApp(int index) {
    if (m_launching || index < 0 || index >= m_app_count) return;

    m_launching = true;
    SetStatus("Requesting launch...");

    paf::ui::Widget *button = root->FindChild(AppButtonId(index));
    if (button) {
        button->SetString(paf::common::string_util::ToWString("LAUNCH..."));
    }

    int result = MoonlightApp::Instance()->Connection().Start(m_apps[index].id);
    if (result != 0) {
        m_launching = false;
        SetStatus("Launch start failed");
        if (button) {
            button->SetString(paf::common::string_util::ToWString("LAUNCH FAILED"));
        }
        return;
    }

    if (button) {
        button->SetString(paf::common::string_util::ToWString("NO STREAM YET"));
    }
    SetStatus("Launch accepted, stream not implemented");
}

void Apps::SetStatus(const char *text) {
    if (!root || !text) return;
    paf::ui::Widget *widget = root->FindChild("text_apps_status");
    if (!widget) return;
    ((paf::ui::Text *)widget)->SetString(
        paf::common::string_util::ToWString(text));
}

void Apps::ShowApps() {
    if (!root) return;

    m_app_count = MoonlightApp::Instance()->Applications().GetAll(m_apps, kMaxApps);
    if (m_app_count < 0) m_app_count = 0;
    if (m_app_count > kMaxApps) m_app_count = kMaxApps;

    for (int i = 0; i < kMaxApps; ++i) {
        paf::ui::Widget *button = root->FindChild(AppButtonId(i));
        if (!button) continue;

        if (i < m_app_count) {
            paf::string label = paf::common::FormatString(
                "%s\n%d",
                m_apps[i].name[0] ? m_apps[i].name : "App",
                m_apps[i].id);
            button->SetString(paf::common::string_util::ToWString(label));
            button->Show(paf::common::transition::Type_Reset);
        } else {
            button->Hide(paf::common::transition::Type_Reset);
        }
    }

    if (m_app_count == 0) {
        SetStatus("No apps returned");
        paf::ui::Widget *button = root->FindChild("btn_app_0");
        if (button) {
            button->SetString(paf::common::string_util::ToWString("NO APPS"));
            button->Show(paf::common::transition::Type_Reset);
        }
    } else {
        SetStatus("Select an app");
    }
}

void Apps::OnMoonlightEvent(const MoonlightEvent *event, void *userdata) {
    Apps *apps = (Apps *)userdata;
    if (!apps || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_APPLICATIONS_READY:
        apps->ShowApps();
        break;
    case MOONLIGHT_EVENT_APPLICATIONS_FAILED: {
        paf::string status = paf::common::FormatString(
            "App list failed: 0x%08X", (unsigned int)event->result);
        apps->SetStatus(status.c_str());
        paf::ui::Widget *button = apps->root->FindChild("btn_app_0");
        if (button) {
            button->SetString(paf::common::string_util::ToWString("APP LIST FAILED"));
            button->Show(paf::common::transition::Type_Reset);
        }
        break;
    }
    case MOONLIGHT_EVENT_STREAM_STARTED:
        apps->SetStatus("Launch accepted, stream not implemented");
        break;
    case MOONLIGHT_EVENT_STREAM_FAILED:
        apps->m_launching = false;
        apps->SetStatus("Launch failed");
        break;
    default:
        break;
    }
}

}
