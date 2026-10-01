#include <paf.h>
#include "pages/page_search.h"
#include "app/moonlight_app.h"

namespace page {

namespace {
static const int kMaxHosts = 8;

static const char *HostButtonId(int index) {
    static const char *ids[kMaxHosts] = {
        "btn_host_0", "btn_host_1", "btn_host_2", "btn_host_3",
        "btn_host_4", "btn_host_5", "btn_host_6", "btn_host_7"
    };
    return (index >= 0 && index < kMaxHosts) ? ids[index] : NULL;
}
}

Search::Search()
    : Base("page_search_pcs", "btn_close_search",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom),
      m_host_count(0) {
    if (!IsValid()) return;

    for (int i = 0; i < kMaxHosts; ++i) {
        m_hosts[i] = MoonlightHost();
        m_button_contexts[i].page = this;
        m_button_contexts[i].index = i;
        paf::ui::Widget *button = root->FindChild(HostButtonId(i));
        if (button) {
            button->SetEventCallback(
                paf::ui::ButtonBase::CB_BTN_DECIDE,
                OnHostButton,
                &m_button_contexts[i]);
            button->Hide(paf::common::transition::Type_Reset);
        }
    }

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    SetStatus("Searching for PCs...");
    MoonlightApp::Instance()->Discovery().Start();
}

Search::~Search() {
    MoonlightApp::Instance()->Discovery().Stop();
    MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
}

void Search::OnHostButton(int32_t type,
                           paf::ui::Handler *self,
                           paf::ui::Event *event,
                           void *userdata) {
    (void)type;
    (void)self;
    (void)event;

    HostButtonContext *context = (HostButtonContext *)userdata;
    if (!context || !context->page) return;
    context->page->SelectHost(context->index);
}

void Search::SelectHost(int index) {
    if (index < 0 || index >= m_host_count) return;

    MoonlightHost host = m_hosts[index];
    SetStatus("Connecting to PC...");

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        SetStatus("Connection failed");
        return;
    }

    if (app->Connection().Connect(host) != 0) {
        SetStatus("Connection failed");
        return;
    }

    SetStatus("Pairing with PC...");
    if (app->Pairing().Pair() != 0) {
        SetStatus("Pairing failed");
    }
}

void Search::SetStatus(const char *text) {
    if (!root || !text) return;
    paf::ui::Widget *widget = root->FindChild("text_search_status");
    if (!widget) return;
    ((paf::ui::Text *)widget)->SetString(
        paf::common::string_util::ToWString(text));
}

void Search::SetHostButton(int index, const MoonlightHost &host) {
    const char *id = HostButtonId(index);
    if (!root || !id) return;

    paf::ui::Widget *widget = root->FindChild(id);
    if (!widget) return;

    paf::string label = paf::common::FormatString(
        "%s\n%s",
        host.name[0] ? host.name : "PC",
        host.internal[0] ? host.internal : "Unknown address");
    widget->SetString(paf::common::string_util::ToWString(label));
    widget->Show(paf::common::transition::Type_Reset);
}

void Search::RefreshHosts() {
    if (!root) return;

    m_host_count = MoonlightApp::Instance()->Discovery().GetHosts(
        m_hosts, kMaxHosts);
    if (m_host_count < 0) m_host_count = 0;
    if (m_host_count > kMaxHosts) m_host_count = kMaxHosts;

    for (int i = 0; i < kMaxHosts; ++i) {
        paf::ui::Widget *widget = root->FindChild(HostButtonId(i));
        if (!widget) continue;

        if (i < m_host_count) {
            SetHostButton(i, m_hosts[i]);
        } else {
            widget->Hide(paf::common::transition::Type_Reset);
        }
    }

    if (m_host_count > 0) SetStatus("PCs found");
}

void Search::OnMoonlightEvent(const MoonlightEvent *event, void *userdata) {
    Search *search = (Search *)userdata;
    if (!search || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_HOST_SCAN_STARTED:
        search->SetStatus("Searching for PCs...");
        break;
    case MOONLIGHT_EVENT_HOSTS_CHANGED:
        search->RefreshHosts();
        break;
    case MOONLIGHT_EVENT_HOST_SCAN_FINISHED:
        search->RefreshHosts();
        if (search->m_host_count == 0) search->SetStatus("No PCs found");
        break;
    case MOONLIGHT_EVENT_HOST_SCAN_FAILED:
        search->SetStatus("PC search failed");
        break;
    case MOONLIGHT_EVENT_PAIRING_FINISHED:
        if (search->m_host_count > 0 && event->host_id >= 0) {
            for (int i = 0; i < search->m_host_count; ++i) {
                if (search->m_hosts[i].id == event->host_id) {
                    int save_result = MoonlightApp::Instance()->Hosts().MarkPaired(search->m_hosts[i]);
                    if (save_result == 0) {
                        search->SetStatus("PC paired");
                    } else {
                        paf::string status = paf::common::FormatString(
                            "PC paired, save failed: 0x%08X", (unsigned int)save_result);
                        search->SetStatus(status.c_str());
                    }
                    return;
                }
            }
        }
        search->SetStatus("PC paired");
        return;
    case MOONLIGHT_EVENT_PAIRING_FAILED:
        search->SetStatus("Pairing failed");
        return;
    default:
        break;
    }
}

}
