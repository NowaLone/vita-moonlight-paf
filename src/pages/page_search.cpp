#include <paf.h>
#include <string.h>

#include "common.h"

#include "pages/page_search.h"
#include "pages/page_apps.h"
#include "pages/page_pairing.h"
#include "pages/page_main.h"
#include "app/moonlight_app.h"

#include "debug.h"

namespace page {

namespace {
static const int kMaxHosts = 8;
}

Search::Search()
    : Base("page_search_pcs", "btn_close_search",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom),
      m_host_count(0),
      m_host_selected(false),
      m_selected_index(-1),
      m_list(NULL) {

    if (!IsValid()) return;

    for (int i = 0; i < kMaxHosts; ++i) {
        m_hosts[i] = MoonlightHost();
        m_button_contexts[i].page = this;
        m_button_contexts[i].index = i;
    }

    m_list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_generic"));

    if (m_list) {
        m_list->SetItemFactory(new ListViewFactory(this));
        m_list->InsertSegment(0, 1);
        m_list->SetCellSizeDefault(0, { 960.0f, 80.0f });
        m_list->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
    }

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    ShowEmptyState();
    int result = MoonlightApp::Instance()->Discovery().Start();
    if (result != 0) {
        MoonlightApp::Instance()->Connection().ShowError("Unable to start PC search.", result);
    }
}

Search::~Search() {
    MoonlightApp::Instance()->Discovery().Stop();

    Main *main = Main::Instance();
    if (main) {
        main->RestoreEventCallback();
    } else {
        MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
    }
}

void Search::RestoreEventCallback() {
    if (MoonlightApp::Instance()->IsInitialized()) {
        /* A completed/cancelled child flow must allow selecting a PC again. */
        m_host_selected = false;
        m_selected_index = -1;
        ShowEmptyState();
        MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    }
}

paf::ui::ListItem *Search::CreateListItem(
    paf::ui::listview::ItemFactory::CreateParam &param) {
    paf::Plugin::TemplateOpenParam tmp;
    g_plugin->TemplateOpen(param.parent, "template_host_list_item", tmp);

    paf::ui::ListItem *item = static_cast<paf::ui::ListItem *>(
        param.parent->GetChild(param.parent->GetChildrenNum() - 1));

    paf::ui::Widget *button = item->FindChild("image_button_host");
    if (!button || param.cell_index < 0 || param.cell_index >= m_host_count) {
        return item;
    }

    button->SetName((uint32_t)param.cell_index);
    button->AddEventCallback(
        paf::ui::ButtonBase::CB_BTN_DECIDE,
        OnHostButton,
        &m_button_contexts[param.cell_index]);

    const MoonlightHost &host = m_hosts[param.cell_index];
    set_child_text(item, "text_host_title", host.name[0] ? host.name : "PC");
    set_child_text(
        item,
        "text_host_desc",
        host.internal[0] ? host.internal : "Unknown address");

    return item;
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
        MoonlightApp::Instance()->Connection().ShowError("Connection failed.");
        return;
    }

    app->Discovery().Stop();

    HideStatus();
    int add_result = app->Hosts().Add(
        host.internal,
        host.port,
        host.name[0] ? host.name : host.internal);

    if (add_result != 0) {
        MoonlightApp::Instance()->Connection().ShowError("Unable to save PC.", add_result);
        m_host_selected = false;
        return;
    }

    /*
     * Add() resolves the discovered entry to the persistent host record and
     * assigns the stable host_id/storage identity. Connect using that
     * canonical record instead of the transient discovery object.
     */
    MoonlightHost canonical_host = host;
    MoonlightHost saved_hosts[kMaxHosts];
    int saved_count = app->Hosts().GetHosts(saved_hosts, kMaxHosts);

    if (saved_count > 0) {
        /*
         * Address is the authoritative match for a freshly discovered host.
         * Name is only a fallback because multiple PCs may share one name.
         */
        int canonical_index = -1;

        if (host.internal[0]) {
            for (int i = 0; i < saved_count && i < kMaxHosts; ++i) {
                if (strcmp(saved_hosts[i].internal, host.internal) == 0) {
                    canonical_index = i;
                    break;
                }
            }
        }

        if (canonical_index < 0 && host.name[0]) {
            for (int i = 0; i < saved_count && i < kMaxHosts; ++i) {
                if (strcmp(saved_hosts[i].name, host.name) == 0) {
                    canonical_index = i;
                    break;
                }
            }
        }

        if (canonical_index >= 0) {
            canonical_host = saved_hosts[canonical_index];
        }
    }

    vita_debug_log(
        "[Search] selected host name=%s ip=%s id=%s saved_count=%d canonical_id=%s",
        host.name,
        host.internal,
        host.host_id,
        saved_count,
        canonical_host.host_id);

    int connect_result = app->Connection().Connect(canonical_host);
    if (connect_result != 0) {
        m_host_selected = false;
        ShowEmptyState();
        return;
    }
}

void Search::OnConnectionReady() {
    MoonlightApp *app = MoonlightApp::Instance();
    if (!app || m_selected_index < 0) {
        m_host_selected = false;
        MoonlightApp::Instance()->Connection().ShowError("Connection failed.");
        return;
    }

    if (app->Connection().State() == MOONLIGHT_CONNECTION_PAIRED) {
        if (!Base::IsOpen("page_apps")) {
            Apps *apps = new Apps();
            if (!apps->IsValid()) {
                delete apps;
                m_host_selected = false;
                MoonlightApp::Instance()->Connection().ShowError("Unable to open applications.");
            }
        }
        return;
    }

    char pin[5] = {0};
    int prepare_result = app->Pairing().Prepare(pin);
    if (prepare_result != 0) {
        MoonlightApp::Instance()->Connection().ShowError("Unable to prepare pairing.", prepare_result);
        m_host_selected = false;
        return;
    }

    Pairing *pairing = new Pairing(pin);
    if (!pairing || !pairing->IsValid()) {
        delete pairing;
        m_host_selected = false;
        MoonlightApp::Instance()->Connection().ShowError("Unable to open pairing screen.");
    }
}

void Search::ShowEmptyState() {
    if (!root) return;
    paf::ui::Widget *widget = root->FindChild("text_search_status");
    if (!widget) return;
    if (m_host_count == 0 && !m_host_selected) {
        widget->Show(paf::common::transition::Type_Reset);
    } else {
        widget->Hide(paf::common::transition::Type_Reset);
    }
}

void Search::HideStatus() {
    if (!root) return;

    paf::ui::Widget *widget = root->FindChild("text_search_status");
    if (widget) {
        widget->Hide(paf::common::transition::Type_Reset);
    }
}

void Search::RefreshHosts() {
    if (!root || m_host_selected || !m_list) return;

    m_host_count = MoonlightApp::Instance()->Discovery().GetHosts(
        m_hosts, kMaxHosts);

    if (m_host_count < 0) m_host_count = 0;
    if (m_host_count > kMaxHosts) m_host_count = kMaxHosts;

    const int existing = m_list->GetCellNum(0);
    if (existing > 0) {
        /* DeleteCell's third argument is a cell count, not the last index. */
        m_list->DeleteCell(0, 0, existing);
    }

    if (m_host_count > 0) {
        m_list->InsertCell(0, 0, m_host_count);
    }
    ShowEmptyState();
}

void Search::OnMoonlightEvent(
    const MoonlightEvent *event,
    void *userdata) {
    Search *search = (Search *)userdata;
    if (!search || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_HOST_SCAN_STARTED:
        break;

    case MOONLIGHT_EVENT_HOSTS_CHANGED:
        search->RefreshHosts();
        break;

    case MOONLIGHT_EVENT_HOST_SCAN_FINISHED:
        if (!search->m_host_selected) {
            search->RefreshHosts();
        }
        break;

    case MOONLIGHT_EVENT_HOST_SCAN_FAILED:
        if (!search->m_host_selected) {
            MoonlightApp::Instance()->Connection().ShowError("PC search failed.", event->result);
        }
        break;

    case MOONLIGHT_EVENT_CONNECTION_READY:
        search->OnConnectionReady();
        break;

    case MOONLIGHT_EVENT_CONNECTION_FAILED:
        search->m_host_selected = false;
        search->m_selected_index = -1;
        search->ShowEmptyState(); /* ConnectionService owns the error dialog. */
        break;

    default:
        break;
    }
}

}
