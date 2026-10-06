#include <paf.h>
#include <string.h>

#include "pages/page_main.h"
#include "pages/page_search.h"
#include "pages/page_add_host.h"
#include "pages/page_apps.h"
#include "pages/page_pairing.h"
#include "option_menu.h"
#include "common.h"
#include "app/moonlight_app.h"

namespace page {

static Main *s_main = NULL;

static void onSearch(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata)
{
    (void)type; (void)self; (void)e; (void)userdata;
    Main *main = Main::Instance();
    if (main) main->OpenSearch();
}

static void onAdd(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata)
{
    (void)type; (void)self; (void)e; (void)userdata;
    Main *main = Main::Instance();
    if (main) main->OpenAddHost();
}

static void onOptionMenu(OptionMenu::EventType type, int button_index, void *userdata)
{
    (void)userdata;
    if (type != OptionMenu::Event_Button || button_index != 0) return;

    Main *main = Main::Instance();
    if (main) main->SuspendForSystemSettings();

    if (MoonlightApp::Instance()->Settings().Open() != 0 && main) {
        main->RestoreAfterSystemSettings();
    }
}

static void onSettingsButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata)
{
    (void)type; (void)self; (void)e;
    Main *main = (Main *)userdata;
    if (OptionMenu::Instance() != NULL) return;
    new OptionMenu(g_plugin, main ? main->root : NULL, onOptionMenu, NULL);
}

Main *Main::Instance()
{
    return s_main;
}

void Main::RestoreEventCallback()
{
    if (MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    }
}

Main::Main()
    : Base("page_main", NULL,
           paf::Plugin::TransitionType_None,
           paf::Plugin::TransitionType_None),
      m_host_count(0),
      m_selected_index(-1),
      m_connecting(false),
      m_initial_refresh_attempts(0)
{
    s_main = this;

    if (!IsValid()) return;

    for (int i = 0; i < 8; ++i) {
        m_hosts[i] = MoonlightHost();
        m_button_contexts[i].page = this;
        m_button_contexts[i].index = i;
    }

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_generic"));
    if (list) {
        list->SetItemFactory(new ListViewFactory(this));
        list->InsertSegment(0, 1);
        list->SetCellSizeDefault(0, { 960.0f, 80.0f });
        list->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
    }

    bind_decide(root, "btn_search_pcs", onSearch, this);
    bind_decide(root, "btn_add_manually", onAdd, this);
    bind_decide(root, "settings_button", onSettingsButton, this);

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    paf::ui::Widget *title = root->FindChild("text_top");
    if (title) {
        ((paf::ui::Text *)title)->SetString(
            paf::common::string_util::ToWString("Connect to a PC"));
    }

    SetStatus("Select a PC to connect.");
    paf::common::MainThreadCallList::Register(InitialRefreshTask, this);
}

void Main::InitialRefreshTask(void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main) {
        return;
    }

    main->RefreshHosts();

    /*
     * PageOpen returns before the first PAF layout pass. Keep refreshing for
     * a short warm-up window so the initial host list is populated on the
     * first visit instead of appearing only after another page is opened.
     */
    if (main->m_host_count > 0 || ++main->m_initial_refresh_attempts >= 60) {
        paf::common::MainThreadCallList::Unregister(InitialRefreshTask, userdata);
    }
}

Main::~Main()
{
    if (MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
    }

    if (s_main == this) {
        s_main = NULL;
    }
}

paf::ui::ListItem *Main::CreateListItem(
    paf::ui::listview::ItemFactory::CreateParam &param)
{
    if (!param.list_view || param.cell_index < 0 ||
        param.cell_index >= m_host_count) {
        return new paf::ui::ListItem(param.parent, NULL);
    }

    paf::Plugin::TemplateOpenParam tmp;
    g_plugin->TemplateOpen(param.parent, "template_list_item_generic", tmp);

    paf::ui::Widget *item = param.parent->GetChild(
        param.parent->GetChildrenNum() - 1);

    paf::ui::Widget *button = item->FindChild("image_button_list_item");
    if (!button) {
        return static_cast<paf::ui::ListItem *>(item);
    }

    button->SetName((uint32_t)param.cell_index);
    button->AddEventCallback(
        paf::ui::ButtonBase::CB_BTN_DECIDE,
        OnHostButton,
        &m_button_contexts[param.cell_index]);

    const MoonlightHost &host = m_hosts[param.cell_index];

    paf::string label = paf::common::FormatString(
        "%s\n%s",
        host.name[0] ? host.name : "PC",
        host.internal[0] ? host.internal : "Unknown host");

    button->SetString(
        paf::common::string_util::ToWString(label));

    return static_cast<paf::ui::ListItem *>(item);
}

void Main::OnHostButton(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)self;
    (void)event;

    HostButtonContext *context = (HostButtonContext *)userdata;
    if (!context || !context->page) return;

    context->page->SelectHost(context->index);
}

void Main::RefreshHosts()
{
    if (!root || m_connecting) return;

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) return;

    m_host_count = app->Hosts().GetHosts(m_hosts, 8);
    if (m_host_count < 0) m_host_count = 0;
    if (m_host_count > 8) m_host_count = 8;

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_generic"));

    if (!list) return;

    int existing = list->GetCellNum(0);
    if (existing > 0) {
        list->DeleteCell(0, 0, existing - 1);
    }

    if (m_host_count > 0) {
        list->InsertCell(0, 0, m_host_count);
        SetStatus("Select a PC to connect.");
    } else {
        SetStatus("No PCs registered.");
    }
}

void Main::SelectHost(int index)
{
    if (m_connecting || index < 0 || index >= m_host_count) return;

    m_selected_index = index;
    m_connecting = true;

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_generic"));
    if (list) {
        list->SetActivate(false);
    }

    paf::string status = paf::common::FormatString(
        "Connecting to %s...",
        m_hosts[index].name[0] ? m_hosts[index].name : m_hosts[index].internal);
    SetStatus(status.c_str());

    int result = MoonlightApp::Instance()->Connection().Connect(m_hosts[index]);
    if (result != 0) {
        m_connecting = false;
        SetStatus("Unable to start connection.");
        if (list) list->SetActivate(true);
    }
}

void Main::SetStatus(const char *text)
{
    if (!root || !text) return;

    paf::ui::Widget *widget = root->FindChild("text_main_status");
    if (!widget) return;

    ((paf::ui::Text *)widget)->SetString(
        paf::common::string_util::ToWString(text));
}

void Main::OpenSearch()
{
    if (Base::IsOpen("page_search_pcs")) return;

    Search *search = new Search();
    if (!search->IsValid()) delete search;
}

void Main::OpenAddHost()
{
    if (Base::IsOpen("page_add_manually")) return;

    AddHost *add = new AddHost();
    if (!add->IsValid()) delete add;
}

void Main::OnMoonlightEvent(const MoonlightEvent *event, void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_HOSTS_CHANGED:
        main->RefreshHosts();
        break;

    case MOONLIGHT_EVENT_CONNECTION_READY:
        main->m_connecting = false;

        if (MoonlightApp::Instance()->Connection().State() ==
            MOONLIGHT_CONNECTION_PAIRED) {
            if (!Base::IsOpen("page_apps")) {
                Apps *apps = new Apps();
                if (!apps->IsValid()) delete apps;
            }
            break;
        }

        if (MoonlightApp::Instance()->Connection().State() ==
            MOONLIGHT_CONNECTION_READY) {
            char pin[5] = {0};
            int prepare_result = MoonlightApp::Instance()->Pairing().Prepare(pin);
            if (prepare_result != 0) {
                main->SetStatus("Unable to prepare pairing.");
                return;
            }

            Pairing *pairing = new Pairing(pin);
            if (!pairing || !pairing->IsValid()) {
                delete pairing;
                main->SetStatus("Unable to open pairing screen.");
            }
            return;
        }

        main->SetStatus("Unable to connect to PC.");
        break;

    case MOONLIGHT_EVENT_CONNECTION_FAILED:
        main->m_connecting = false;
        main->SetStatus("Unable to connect to PC.");

        {
            paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
                main->root->FindChild("list_view_generic"));
            if (list) list->SetActivate(true);
        }
        break;

    case MOONLIGHT_EVENT_CONNECTION_CLOSED:
        main->m_connecting = false;
        main->SetStatus("Connection closed.");
        {
            paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
                main->root->FindChild("list_view_generic"));
            if (list) list->SetActivate(true);
        }
        break;

    case MOONLIGHT_EVENT_SETTINGS_CLOSED:
        main->RestoreAfterSystemSettings();
        main->RefreshHosts();
        break;

    case MOONLIGHT_EVENT_PAIRING_REQUIRED:
        /*
         * Saved unpaired hosts are uncommon, but keep the UI truthful rather
         * than silently doing nothing.
         */
        main->m_connecting = false;
        main->SetStatus("This PC needs pairing. Use Search PCs.");
        {
            paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
                main->root->FindChild("list_view_generic"));
            if (list) list->SetActivate(true);
        }
        break;

    default:
        break;
    }
}

void Main::SuspendForSystemSettings()
{
    if (!root) return;
    root->SetActivate(false);
}

void Main::RestoreAfterSystemSettings()
{
    if (!root) return;
    root->SetActivate(true);
}

}
