#include <paf.h>
#include <string.h>

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
      m_host_selected(false),
      m_pairing_pending(false),
      m_selected_index(-1) {
    m_pairing_pin[0] = '\0';

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

    Search *search = context->page;

    if (search->m_pairing_pending &&
        context->index == search->m_selected_index) {
        search->ContinuePairing();
        return;
    }

    if (search->m_host_selected) {
        return;
    }

    search->SelectHost(context->index);
}

void Search::SelectHost(int index) {
    if (m_host_selected || index < 0 || index >= m_host_count) return;

    m_host_selected = true;
    m_selected_index = index;
    MoonlightHost host = m_hosts[index];

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        m_host_selected = false;
        SetStatus("Connection failed");
        return;
    }

    app->Discovery().Stop();

    SetStatus("Saving PC...");
    int add_result = app->Hosts().Add(
        host.internal,
        host.port,
        host.name[0] ? host.name : host.internal);

    paf::ui::Widget *selected_button = root->FindChild(HostButtonId(index));
    if (selected_button) {
        paf::string result_label = paf::common::FormatString(
            add_result == 0 ? "SAVED 0x00000000\\n%s" : "SAVE ERROR 0x%08X\\n%s",
            (unsigned int)add_result,
            host.internal);
        selected_button->SetString(
            paf::common::string_util::ToWString(result_label));
    }

    if (add_result != 0) {
        m_host_selected = false;
        return;
    }

    SetStatus("Connecting to PC...");
    int connect_result = app->Connection().Connect(host);
    if (connect_result != 0) {
        paf::string status = paf::common::FormatString(
            "Connection failed: 0x%08X", (unsigned int)connect_result);
        SetStatus(status.c_str());
        m_host_selected = false;
        return;
    }

    char pin[5] = {0};
    int prepare_result = app->Pairing().Prepare(pin);
    if (prepare_result != 0) {
        paf::string status = paf::common::FormatString(
            "Pairing prepare failed: 0x%08X", (unsigned int)prepare_result);
        SetStatus(status.c_str());
        m_host_selected = false;
        return;
    }

    memcpy(m_pairing_pin, pin, sizeof(m_pairing_pin));
    m_pairing_pending = true;

    if (selected_button) {
        paf::string pairing_label = paf::common::FormatString(
            "PIN %s\\nEnter on PC, then press X",
            m_pairing_pin);
        selected_button->SetString(
            paf::common::string_util::ToWString(pairing_label));
    }

    SetStatus("Enter the PIN on the PC, then press X");
}

void Search::ContinuePairing() {
    if (!m_pairing_pending || m_pairing_pin[0] == '\0') {
        return;
    }

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        SetStatus("Connection failed");
        return;
    }

    int result = app->Pairing().Pair(m_pairing_pin);
    if (result != 0) {
        paf::string status = paf::common::FormatString(
            "Pairing start failed: 0x%08X", (unsigned int)result);
        SetStatus(status.c_str());
        return;
    }

    m_pairing_pending = false;
    SetStatus("Pairing with PC...");

    paf::ui::Widget *button = root->FindChild(HostButtonId(m_selected_index));
    if (button) {
        button->SetString(
            paf::common::string_util::ToWString("PAIRING..."));
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
        "%s\\n%s",
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
    if (!search || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_HOST_SCAN_STARTED:
        if (!search->m_host_selected) search->SetStatus("Searching for PCs...");
        break;
    case MOONLIGHT_EVENT_HOSTS_CHANGED:
        if (!search->m_host_selected) search->RefreshHosts();
        break;
    case MOONLIGHT_EVENT_HOST_SCAN_FINISHED:
        if (!search->m_host_selected) {
            search->RefreshHosts();
            if (search->m_host_count == 0) search->SetStatus("No PCs found");
        }
        break;
    case MOONLIGHT_EVENT_HOST_SCAN_FAILED:
        if (!search->m_host_selected) search->SetStatus("PC search failed");
        break;
    case MOONLIGHT_EVENT_PAIRING_REQUIRED:
        if (search->m_selected_index >= 0 && event->pairing_pin[0]) {
            memcpy(search->m_pairing_pin, event->pairing_pin, sizeof(search->m_pairing_pin));
            search->m_pairing_pending = true;

            paf::ui::Widget *button = search->root->FindChild(
                HostButtonId(search->m_selected_index));
            if (button) {
                paf::string pairing_label = paf::common::FormatString(
                    "PIN %s\\nEnter on PC, then press X",
                    search->m_pairing_pin);
                button->SetString(
                    paf::common::string_util::ToWString(pairing_label));
            }

            search->SetStatus("Enter the PIN on the PC, then press X");
        }
        break;
    case MOONLIGHT_EVENT_PAIRING_FINISHED:
        if (event->result == 0) {
            search->SetStatus("PC paired and saved");
        } else {
            paf::string status = paf::common::FormatString(
                "Paired, local save failed: 0x%08X",
                (unsigned int)event->result);
            search->SetStatus(status.c_str());
        }
        break;
    case MOONLIGHT_EVENT_PAIRING_FAILED: {
        paf::string status = paf::common::FormatString(
            "Pairing failed: 0x%08X", (unsigned int)event->result);
        search->SetStatus(status.c_str());
        break;
    }
    case MOONLIGHT_EVENT_CONNECTION_FAILED: {
        if (search->m_host_selected) {
            paf::string status = paf::common::FormatString(
                "Connection failed: 0x%08X", (unsigned int)event->result);
            search->SetStatus(status.c_str());
        }
        break;
    }
    default:
        break;
    }
}

}
