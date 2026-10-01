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
      m_host_count(0),
      m_host_selected(false) {
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

    // Make the callback observable before touching networking/storage.
    if (context->page->root) {
        const char *button_id = HostButtonId(context->index);
        paf::ui::Widget *button = context->page->root->FindChild(button_id);
        if (button) {
            button->SetString(
                paf::common::string_util::ToWString("SELECTED"));
        }
    }

    context->page->SetStatus("HOST SELECTED");
    context->page->SelectHost(context->index);
}

void Search::SelectHost(int index) {
    if (m_host_selected || index < 0 || index >= m_host_count) return;

    m_host_selected = true;
    MoonlightHost host = m_hosts[index];
    SetStatus("Saving PC...");

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        SetStatus("Connection failed");
        return;
    }

    // Discovery is no longer needed once a host has been selected.
    // Stopping it prevents late scan events from overwriting the status.
    app->Discovery().Stop();

    SetStatus("Saving PC...");
    int add_result = app->Hosts().Add(
        host.internal,
        host.port,
        host.name[0] ? host.name : host.internal);
    if (add_result != 0) {
        paf::string status = paf::common::FormatString(
            "Save failed: 0x%08X", (unsigned int)add_result);
        SetStatus(status.c_str());
        return;
    }

    SetStatus("Connecting to PC...");
    if (app->Connection().Connect(host) != 0) {
        SetStatus("Connection failed");
        return;
    }

    SetStatus("Pairing with PC...");
    int pair_result = app->Pairing().Pair();
    if (pair_result != 0) {
        paf::string status = paf::common::FormatString(
            "Pairing failed: 0x%08X", (unsigned int)pair_result);
        SetStatus(status.c_str());
        return;
    }

    // The current backend pairing call is synchronous. Persist immediately
    // so the UI reports the actual storage result instead of waiting for the
    // queued pairing event.
    int save_result = app->Hosts().MarkPaired(host);
    if (save_result == 0) {
        SetStatus("PC paired and saved");
    } else {
        paf::string status = paf::common::FormatString(
            "Paired, save failed: 0x%08X", (unsigned int)save_result);
        SetStatus(status.c_str());
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
    if (!root || m_host_selected) return;

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
    if (!search || !event || search->m_host_selected) return;

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
        // Persistence is handled synchronously by SelectHost().
        return;
    case MOONLIGHT_EVENT_PAIRING_FAILED:
        search->SetStatus("Pairing failed");
        return;
    default:
        break;
    }
}

}
