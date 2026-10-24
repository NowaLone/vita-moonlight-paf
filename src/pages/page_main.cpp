#include <paf.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <psp2/common_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/sysmodule.h>

#include "pages/page_main.h"
#include "pages/host_list.h"
#include "pages/host_availability.h"
#include "pages/page_apps.h"
#include "pages/page_pairing.h"
#include "option_menu.h"
#include "common.h"
#include "app/moonlight_app.h"
#include "debug.h"

namespace page {

static Main *s_main = NULL;

namespace {

static const int kGameStreamPort = 47989;
static const int kMaxHostAddressLength = 255;

static const SceWChar16 kImeTitle[] = {
    'P', 'C', ' ', 'a', 'd', 'd', 'r', 'e', 's', 's', 0
};

static void ConvertAsciiToImeText(
    const char *input,
    SceWChar16 *output,
    size_t capacity)
{
    if (!output || capacity == 0) {
        return;
    }

    output[0] = 0;
    if (!input) {
        return;
    }

    size_t i = 0;
    while (input[i] != '\0' && i + 1 < capacity) {
        output[i] = (SceWChar16)(unsigned char)input[i];
        ++i;
    }
    output[i] = 0;
}

static bool ConvertImeTextToAddress(
    const SceWChar16 *input,
    char *output,
    size_t output_size)
{
    if (!input || !output || output_size < 2) {
        return false;
    }

    size_t length = 0;
    while (input[length] != 0) {
        if (input[length] < 0x21 || input[length] > 0x7e) {
            return false;
        }
        if (length + 1 >= output_size) {
            return false;
        }
        output[length] = (char)input[length];
        ++length;
    }

    output[length] = '\0';
    return length > 0;
}

static bool ParsePort(const char *text, uint16_t *port)
{
    unsigned long value;

    if (!text || !text[0] || !port) {
        return false;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }

    value = strtoul(text, NULL, 10);
    if (value == 0 || value > 65535) {
        return false;
    }

    *port = (uint16_t)value;
    return true;
}

static bool ParseHostPort(
    const char *input,
    char *host,
    size_t host_size,
    uint16_t *port)
{
    const char *last_colon;
    const char *first_colon;
    const char *port_text;
    size_t host_length;

    if (!input || !input[0] || !host || host_size < 2 || !port) {
        return false;
    }

    *port = (uint16_t)kGameStreamPort;

    first_colon = strchr(input, ':');
    last_colon = strrchr(input, ':');

    if (first_colon == NULL) {
        host_length = strlen(input);
        if (host_length >= host_size) {
            return false;
        }
        memcpy(host, input, host_length + 1);
        return true;
    }

    if (first_colon != last_colon) {
        /*
         * The current legacy GameStream URL builder does not support raw IPv6
         * literals. Keep this input format unambiguous instead of accepting
         * an address that will fail later in URL construction.
         */
        return false;
    }

    host_length = (size_t)(last_colon - input);
    if (host_length == 0 || host_length >= host_size) {
        return false;
    }

    port_text = last_colon + 1;
    if (!ParsePort(port_text, port)) {
        return false;
    }

    memcpy(host, input, host_length);
    host[host_length] = '\0';
    return true;
}

}

static void onAdd(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata)
{
    (void)type; (void)self; (void)e; (void)userdata;
    Main *main = Main::Instance();
    if (main) main->OpenAddPc();
}

static void onOptionMenu(OptionMenu::EventType type, int button_index, void *userdata)
{
    (void)userdata;
    if (type != OptionMenu::Event_Button) return;

    Main *main = Main::Instance();
    if (!main) return;

    switch (button_index) {
    case OptionMenu::Button_Settings:
        main->SuspendForSystemSettings();
        break;

    case OptionMenu::Button_Delete:
        main->EnterSelectionMode();
        break;

    default:
        break;
    }
}

static void onSettingsButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata)
{
    (void)type; (void)self; (void)e;
    Main *main = (Main *)userdata;
    if (OptionMenu::Instance() != NULL) return;
    if (main) main->SuspendBackground();
    new OptionMenu(
        onOptionMenu,
        NULL,
        main != NULL && main->HasHosts());
}

Main *Main::Instance()
{
    return s_main;
}

void Main::RestoreEventCallback()
{
    if (MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
        m_connecting = false;
        RefreshHosts();
        paf::ui::Widget *list = root ? root->FindChild("list_view_main") : NULL;
        if (list) list->SetActivate(true);
    }
}

Main::Main()
    : Base("page_main", NULL,
           paf::Plugin::TransitionType_None,
           paf::Plugin::TransitionType_None),
      m_next_host_token(0),
      m_host_count(0),
      m_saved_count(0),
      m_pending_action(Pending_None),
      m_discovery_retry_at(0),
      m_discovery_error(0),
      m_app_state_poll_at(0),
      m_system_ui_overlaid(true),
      m_list_ready(false),
      m_connecting(false),
      m_settings_suspended(false),
      m_initial_refresh_attempts(0),
      m_initial_refresh_stable_count(0),
      m_ime_open(false),
      m_ime_task_registered(false),
      m_ime_retry_pending(false),
      m_ime_module_loaded(false),
      m_selection_mode(false),
      m_selected_count(0),
      m_delete_dialog_open(false),
      m_delete_dialog_task_registered(false)
{
    memset(&m_ime_param, 0, sizeof(m_ime_param));
    memset(m_ime_input, 0, sizeof(m_ime_input));
    memset(m_ime_initial_text, 0, sizeof(m_ime_initial_text));
    memset(m_add_pc_address, 0, sizeof(m_add_pc_address));
    memset(m_delete_message, 0, sizeof(m_delete_message));

    s_main = this;

    if (!IsValid()) return;

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_main"));
    if (list) {
        list->SetItemFactory(new ListViewFactory(this));
        list->InsertSegment(0, 1);
        list->SetCellSizeDefault(0, { 960.0f, 80.0f });
        list->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
    }

    bind_decide(root, "btn_add_pc", onAdd, this);
    bind_decide(root, "settings_button", onSettingsButton, this);
    bind_decide(root, "btn_selection_cancel", OnSelectionCancel, this);
    bind_decide(root, "btn_selection_action", OnSelectionAction, this);
    bind_decide(root, "btn_selection_select_all", OnSelectionSelectAll, this);

    root->FindChild("plane_main_selection_actions")->Hide(
        paf::common::transition::Type_Reset);

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    paf::ui::Widget *title = root->FindChild("text_top");
    if (title) {
        ((paf::ui::Text *)title)->SetString(
            paf::common::string_util::ToWString("Connect to a PC"));
    }

    HideStatus();
    paf::common::MainThreadCallList::Register(InitialRefreshTask, this);
    paf::common::MainThreadCallList::Register(AvailabilityTask, this);
    paf::common::MainThreadCallList::Register(BackgroundTask, this);
}

void Main::SuspendBackground()
{
    MoonlightApp::Instance()->Discovery().Stop();
    MoonlightApp::Instance()->Hosts().StopAvailability(this);
}

bool Main::CanDiscover() const
{
    return Base::GetCurrent() == this && m_list_ready && !m_system_ui_overlaid &&
           !m_connecting && !m_selection_mode && !m_settings_suspended &&
           !m_ime_open && !m_ime_retry_pending && m_pending_action == Pending_None &&
           !m_delete_dialog_open && !MoonlightApp::Instance()->Connection().IsBusy();
}

void Main::BackgroundTask(void *userdata)
{
    Main *main = static_cast<Main *>(userdata);
    MoonlightApp *app = MoonlightApp::Instance();
    if (!app->IsInitialized()) return;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (now >= main->m_app_state_poll_at) {
        SceAppMgrAppState state;
        memset(&state, 0, sizeof(state));
        int result = _sceAppMgrGetAppState(&state, sizeof(state), 0);
        /* Failed lifecycle reads cannot authorize background networking. */
        main->m_system_ui_overlaid = result < 0 || state.isSystemUiOverlaid != 0;
        main->m_app_state_poll_at = now + 100000;
    }
    bool visible = Base::GetCurrent() == main && !main->m_system_ui_overlaid;
    if (visible && main->m_pending_action != Pending_None && app->Discovery().IsIdle()) {
        PendingAction action = main->m_pending_action;
        main->m_pending_action = Pending_None;
        if (action == Pending_AddPc) {
            if (main->root) main->root->SetActivate(true);
            main->OpenAddPc();
        } else if (app->Settings().Open() != 0) {
            main->RestoreAfterSystemSettings();
        }
    }
    if (!main->CanDiscover()) {
        main->SuspendBackground();
        return;
    }
    if (now < main->m_discovery_retry_at) return;
    int result = app->Discovery().Start();
    if (result < 0) {
        if (result != main->m_discovery_error)
            vita_debug_log("[Main] background discovery start error=%d", result);
        main->m_discovery_retry_at = now + 5000000;
    }
    main->m_discovery_error = result;
}

void Main::AvailabilityTask(void *userdata)
{
    Main *main = static_cast<Main *>(userdata);
    MoonlightApp *app = MoonlightApp::Instance();
    if (!app->IsInitialized() || Base::GetCurrent() != main || main->m_connecting ||
        main->m_settings_suspended || main->m_ime_open || main->m_ime_retry_pending ||
        main->m_delete_dialog_open || main->m_system_ui_overlaid ||
        main->m_pending_action != Pending_None || app->Connection().IsBusy()) {
        app->Hosts().StopAvailability(main);
        return;
    }
    app->Hosts().CheckAvailability(main, main->m_saved_hosts);
}

void Main::InitialRefreshTask(void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main) {
        return;
    }

    ++main->m_initial_refresh_attempts;

    /*
     * Give PAF a few main-thread ticks between list rebuilds so its layout can
     * settle instead of deleting/reinserting cells on every tick.
     */
    if (main->m_initial_refresh_attempts == 1 ||
        main->m_initial_refresh_attempts % 3 == 0) {
        main->RefreshHosts();
    }

    /*
     * PageOpen can return before the ListView has completed its first layout.
     * A non-empty backend result alone is not enough to stop the warm-up:
     * verify that PAF has created the matching number of cells, and that the
     * result stays stable across several main-thread ticks.
     */
    paf::ui::ListView *list = main->root
        ? static_cast<paf::ui::ListView *>(
              main->root->FindChild("list_view_main"))
        : NULL;
    int cell_count = list ? list->GetCellNum(0) : -1;

    if (main->m_host_count > 0 &&
        cell_count == main->m_host_count) {
        ++main->m_initial_refresh_stable_count;
    } else {
        main->m_initial_refresh_stable_count = 0;
    }

    const bool list_is_stable =
        main->m_initial_refresh_attempts >= 10 &&
        main->m_initial_refresh_stable_count >= 3;
    const bool warmup_expired =
        main->m_initial_refresh_attempts >= 120;

    if (list_is_stable || warmup_expired) {
        vita_debug_log(
            "[Main] initial refresh finished attempts=%d hosts=%d cells=%d stable=%d timeout=%d",
            main->m_initial_refresh_attempts,
            main->m_host_count,
            cell_count,
            main->m_initial_refresh_stable_count,
            warmup_expired ? 1 : 0);
        main->m_list_ready = true;
        paf::common::MainThreadCallList::Unregister(InitialRefreshTask, userdata);
    }
}

Main::~Main()
{
    SuspendBackground();
    paf::common::MainThreadCallList::Unregister(BackgroundTask, this);
    paf::common::MainThreadCallList::Unregister(InitialRefreshTask, this);
    paf::common::MainThreadCallList::Unregister(AvailabilityTask, this);
    MoonlightApp::Instance()->Hosts().StopAvailability(this);

    if (m_delete_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(SelectionDialogPollTask, this);
        m_delete_dialog_task_registered = false;
    }

    if (m_delete_dialog_open) {
        sceMsgDialogAbort();
        if (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
            sceMsgDialogTerm();
        }
        m_delete_dialog_open = false;
    }

    if (m_ime_task_registered) {
        paf::common::MainThreadCallList::Unregister(ImePollTask, this);
        m_ime_task_registered = false;
    }
    m_ime_retry_pending = false;

    if (m_ime_open) {
        sceImeDialogAbort();
        sceImeDialogTerm();
        m_ime_open = false;
    }

    if (m_ime_module_loaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
        m_ime_module_loaded = false;
    }

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

    /*
     * Checkboxes live in a dedicated template instead of being shown/hidden
     * on the shared one. A mode/saved-state change replaces only affected cells.
     */
    paf::Plugin::TemplateOpenParam tmp;
    g_plugin->TemplateOpen(
        param.parent,
        m_selection_mode
            ? "template_host_list_item_select"
            : (m_rows[param.cell_index].saved
                ? "template_host_list_item" : "template_host_list_item_discovered"),
        tmp);

    paf::ui::Widget *item = param.parent->GetChild(
        param.parent->GetChildrenNum() - 1);

    paf::ui::Widget *button = item->FindChild(
        m_selection_mode
            ? "image_button_host_select"
            : "image_button_host");
    if (!button) {
        return static_cast<paf::ui::ListItem *>(item);
    }

    button->SetName(m_rows[param.cell_index].token);
    button->AddEventCallback(
        paf::ui::ButtonBase::CB_BTN_DECIDE,
        OnHostButton,
        this);

    if (m_selection_mode) {
        paf::ui::Widget *checkbox_widget = item->FindChild("checkbox_host_select");
        if (checkbox_widget) {
            paf::ui::CheckBox *checkbox = (paf::ui::CheckBox *)checkbox_widget;
            checkbox->SetName(m_rows[param.cell_index].checkbox_token);
            checkbox->SetCheck(
                m_rows[param.cell_index].selected,
                0.0f,
                false);
            checkbox->AddEventCallback(
                paf::ui::CheckBox::CB_BTN_DECIDE,
                OnHostSelection,
                this);

            /* Focus stays on the row; the checkbox mirrors its state. */
            set_widget_focusable(checkbox, false);
        }
    }

    UpdateHostItem(item, param.cell_index);

    return static_cast<paf::ui::ListItem *>(item);
}

void Main::UpdateHostItem(paf::ui::Widget *item, int index)
{
    if (!item) return;
    const MoonlightHost &host = m_hosts[index];
    if (m_rows[index].saved) set_host_availability(item, host);
    set_child_text(item, m_selection_mode ? "text_host_select_title" : "text_host_title",
                   host.name[0] ? host.name : "PC");
    set_child_text(item, m_selection_mode ? "text_host_select_desc" : "text_host_desc",
                   host.internal[0] ? host.internal : "Unknown host");
    /* SetName() replaced the template ID with this row's callback token. */
    paf::ui::Widget *checkbox = item->FindChild(m_rows[index].checkbox_token);
    if (checkbox) static_cast<paf::ui::CheckBox *>(checkbox)->SetCheck(
        m_rows[index].selected, 0.0f, false);
}

void Main::OnHostButton(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)event;

    Main *main = static_cast<Main *>(userdata);
    paf::ui::Widget *button = static_cast<paf::ui::Widget *>(self);
    if (!main || !button) return;
    int index = main->FindHostIndex(button->GetName().GetIDHash());
    if (index < 0) return;

    if (main->m_selection_mode) {
        main->ToggleHostSelection(index);
        return;
    }
    main->SelectHost(index);
}

int Main::FindHostIndex(uint32_t token) const
{
    for (int i = 0; i < m_host_count; ++i) {
        if (m_rows[i].token == token || m_rows[i].checkbox_token == token) return i;
    }
    return -1;
}

void Main::OnHostSelection(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)event;
    Main *main = static_cast<Main *>(userdata);
    paf::ui::CheckBox *checkbox = static_cast<paf::ui::CheckBox *>(self);
    if (!main || !checkbox || !main->m_selection_mode) return;
    int index = main->FindHostIndex(checkbox->GetName().GetIDHash());
    if (index < 0) return;

    bool checked = checkbox->IsChecked();
    if (main->m_rows[index].selected != checked) {
        main->m_rows[index].selected = checked;
        main->UpdateSelectionCount();
    }
}

bool Main::ReadHostRows(paf::vector<MoonlightHost> &hosts)
{
    MoonlightApp *app = MoonlightApp::Instance();
    paf::vector<MoonlightHost> saved;
    if (!read_host_snapshot(app->Hosts(), saved)) { HideStatus(); return false; }
    m_saved_count = (int)saved.size();
    m_saved_hosts.swap(saved);
    for (int i = 0; i < m_saved_count; ++i) hosts.push_back(m_saved_hosts[i]);
    if (!m_selection_mode) {
        paf::vector<MoonlightHost> discovered;
        if (!read_host_snapshot(app->Discovery(), discovered)) return false;
        for (int i = 0; i < (int)discovered.size(); ++i) {
            bool duplicate = false;
            for (int j = 0; j < (int)hosts.size(); ++j) {
                if (same_discovered_host(hosts[j], discovered[i])) { duplicate = true; break; }
            }
            if (!duplicate) hosts.push_back(discovered[i]);
        }
    }
    return true;
}

void Main::RefreshHosts()
{
    if (!root || m_connecting) return;
    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_main"));
    if (!list) return;
    paf::vector<MoonlightHost> hosts;
    if (!ReadHostRows(hosts)) return;
    paf::vector<HostRowState> rows;
    rows.resize(hosts.size());
    for (int i = 0; i < (int)hosts.size(); ++i) {
        rows[i].saved = i < m_saved_count;
        rows[i].selection_template = m_selection_mode;
        for (int j = 0; j < m_host_count; ++j) {
            if (!same_discovered_host(hosts[i], m_hosts[j])) continue;
            rows[i].selected = m_rows[j].selected;
            if (rows[i].saved == m_rows[j].saved &&
                rows[i].selection_template == m_rows[j].selection_template)
                rows[i] = m_rows[j];
            break;
        }
        if (!rows[i].token) {
            if (++m_next_host_token == 0) ++m_next_host_token;
            rows[i].token = m_next_host_token;
            if (++m_next_host_token == 0) ++m_next_host_token;
            rows[i].checkbox_token = m_next_host_token;
        }
    }

    paf::vector<uint32_t> cells;
    int focused_index = -1;
    MoonlightHost focused_host = {};
    uint32_t focused_token = 0;
    for (int i = 0; i < m_host_count; ++i) {
        cells.push_back(m_rows[i].token);
        paf::ui::ListItem *item = list->GetListItem(0, i);
        paf::ui::Widget *button = item ? item->FindChild(m_rows[i].token) : NULL;
        if (button && button->IsFocused()) {
            focused_index = i;
            focused_host = m_hosts[i];
            focused_token = m_rows[i].token;
        }
    }
    /* Block native generation while the final backing snapshot and cell edits change.
     * Retained rows keep their tokens/widgets; removed/retemplated tokens are invalid now. */
    list->BlockSync();
    m_hosts.swap(hosts);
    m_rows.swap(rows);
    m_host_count = (int)m_hosts.size();
    paf::vector<uint32_t> desired;
    for (int i = 0; i < m_host_count; ++i) desired.push_back(m_rows[i].token);
    int from = 0, to = 0, edit;
    while ((edit = next_host_cell_edit(cells.data(), (int)cells.size(),
                                       desired.data(), m_host_count, &from, &to)) != 0) {
        if (edit == 1) {
            list->DeleteCell(0, from, 1);
            for (int i = from; i + 1 < (int)cells.size(); ++i) cells[i] = cells[i + 1];
            cells.pop_back();
        } else if (edit == 2) {
            uint32_t token = cells[from];
            /* The planner moves an existing later cell toward the desired prefix. */
            for (int i = from; i > to; --i) cells[i] = cells[i - 1];
            cells[to] = token;
            list->MoveCell(0, from, to);
        } else {
            cells.push_back(0);
            /* Shift backwards: the installed PAF vector's insert uses forward move. */
            for (int i = (int)cells.size() - 1; i > to; --i) cells[i] = cells[i - 1];
            cells[to] = desired[to];
            list->InsertCell(0, to, 1);
        }
    }
    list->UnblockSync();
    for (int i = 0; i < m_host_count; ++i) {
        int previous = -1;
        for (int j = 0; j < (int)rows.size(); ++j)
            if (rows[j].token == m_rows[i].token) { previous = j; break; }
        if (previous < 0 || strcmp(hosts[previous].name, m_hosts[i].name) != 0 ||
            strcmp(hosts[previous].internal, m_hosts[i].internal) != 0 ||
            rows[previous].selected != m_rows[i].selected)
            UpdateHostItem(list->GetListItem(0, i), i);
    }
    /* Native surviving cells retain focus and viewport. Only a removed/retemplated
     * focused cell needs a replacement: same PC, then the nearest remaining index. */
    if (focused_index >= 0 && FindHostIndex(focused_token) < 0 && m_host_count > 0) {
        int target = focused_index < m_host_count ? focused_index : m_host_count - 1;
        for (int i = 0; i < m_host_count; ++i)
            if (same_discovered_host(focused_host, m_hosts[i])) { target = i; break; }
        list->SetFocus(0, target, paf::ui::ListView::FOCUS_ALIGN_TYPE_HEAD, NULL);
    }
    if (m_selection_mode) UpdateSelectionCount();
    ShowIdleStatus();
}

void Main::SelectHost(int index)
{
    MoonlightApp *app = MoonlightApp::Instance();
    if (m_connecting || m_selection_mode || m_ime_open || m_ime_retry_pending ||
        m_pending_action != Pending_None || app->Connection().IsBusy() ||
        index < 0 || index >= m_host_count) return;
    /* Copy before any save/event/refresh can reorder the list. */
    MoonlightHost host = m_hosts[index];
    bool saved = m_rows[index].saved;
    m_connecting = true;
    SuspendBackground();
    if (!saved) {
        int result = app->Hosts().Add(host.internal, host.port,
                                      host.name[0] ? host.name : host.internal);
        if (result != 0) {
            m_connecting = false;
            app->Connection().ShowError("Unable to save PC.", result);
            return;
        }
        paf::vector<MoonlightHost> saved_hosts;
        if (!read_host_snapshot(app->Hosts(), saved_hosts)) {
            m_connecting = false;
            app->Connection().ShowError("Unable to read saved PCs.");
            return;
        }
        int canonical = canonical_host_index(saved_hosts.data(), (int)saved_hosts.size(), host);
        if (canonical < 0) {
            m_connecting = false;
            app->Connection().ShowError("Unable to find saved PC.");
            return;
        }
        host = saved_hosts[canonical];
    }
    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_main"));
    if (list) list->SetActivate(false);
    HideStatus();
    int result = app->Connection().Connect(host);
    if (result != 0) {
        m_connecting = false;
        RefreshHosts();
        if (list) list->SetActivate(true);
    }
}

void Main::ToggleHostSelection(int index)
{
    if (!m_selection_mode || index < 0 || index >= m_host_count) {
        return;
    }

    /*
     * Update the flag first: if SetCheck() notifies OnHostSelection(), that
     * handler sees the checkbox already matches and changes nothing.
     */
    m_rows[index].selected = !m_rows[index].selected;

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(root->FindChild("list_view_main"));
    UpdateHostItem(list ? list->GetListItem(0, index) : NULL, index);

    UpdateSelectionCount();
}

bool Main::AllHostsSelected() const
{
    return m_host_count > 0 && m_selected_count >= m_host_count;
}

void Main::ToggleSelectAll()
{
    if (!m_selection_mode || m_host_count <= 0) {
        return;
    }

    /* Same condition as the button label: "Clear all" only when everything is selected. */
    const bool select = !AllHostsSelected();
    for (int i = 0; i < m_host_count; ++i) {
        m_rows[i].selected = select;
    }

    UpdateSelectionCount();

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(root->FindChild("list_view_main"));
    for (int i = 0; i < m_host_count; ++i)
        UpdateHostItem(list ? list->GetListItem(0, i) : NULL, i);
}

void Main::UpdateSelectionCount()
{
    m_selected_count = 0;
    for (int i = 0; i < m_host_count; ++i) {
        if (m_rows[i].selected) {
            ++m_selected_count;
        }
    }

    UpdateSelectionActionBar();
}

void Main::UpdateSelectionActionBar()
{
    if (!root || !m_selection_mode) {
        return;
    }

    paf::ui::Widget *action = root->FindChild("btn_selection_action");
    if (action) {
        set_button_enabled(action, m_selected_count > 0);
    }

    paf::ui::Widget *select_all = root->FindChild("btn_selection_select_all");
    if (select_all) {
        /*
         * The button is auto-sized from its label (adjust "2, 0, 0" in the XML),
         * so no width is set here.
         */
        select_all->SetString(paf::common::string_util::ToWString(
            AllHostsSelected() ? "Clear All" : "Select All"));
    }
}

void Main::EnterSelectionMode()
{
    if (m_connecting || m_ime_open || m_ime_retry_pending ||
        m_saved_count <= 0) {
        return;
    }

    SuspendBackground();
    m_selection_mode = true;
    m_selected_count = 0;
    for (int i = 0; i < m_host_count; ++i) m_rows[i].selected = false;

    paf::ui::Widget *add_pc = root->FindChild("btn_add_pc");
    paf::ui::Widget *status = root->FindChild("text_main_status");
    paf::ui::Widget *settings = root->FindChild("settings_button");
    paf::ui::Widget *selection_actions = root->FindChild("plane_main_selection_actions");

    set_button_enabled(add_pc, false);
    if (status) status->Hide(paf::common::transition::Type_Reset);
    if (settings) settings->Hide(paf::common::transition::Type_Reset);

    /*
     * Circle is PAD_ESCAPE (0x20). Bind it before the bar is shown so the
     * direct key is live on the first back press, including while a list row
     * has focus. Photos uses the same mapping for the selection-bar Cancel.
     */
    paf::ui::Widget *cancel = root->FindChild("btn_selection_cancel");
    if (cancel) {
        cancel->SetKeycode(paf::inputdevice::pad::Data::PAD_ESCAPE);
    }

    if (selection_actions) selection_actions->Show(paf::common::transition::Type_Reset);

    RefreshHosts();
    UpdateSelectionActionBar();
}

void Main::ExitSelectionMode()
{
    if (!m_selection_mode) {
        return;
    }

    m_selection_mode = false;
    m_selected_count = 0;
    for (int i = 0; i < m_host_count; ++i) m_rows[i].selected = false;

    paf::ui::Widget *add_pc = root->FindChild("btn_add_pc");
    paf::ui::Widget *settings = root->FindChild("settings_button");
    paf::ui::Widget *selection_actions = root->FindChild("plane_main_selection_actions");

    paf::ui::Widget *cancel = root->FindChild("btn_selection_cancel");
    if (cancel) {
        cancel->SetKeycode(0);
    }

    if (selection_actions) selection_actions->Hide(paf::common::transition::Type_Reset);
    set_button_enabled(add_pc, true);
    if (settings) settings->Show(paf::common::transition::Type_Reset);

    RefreshHosts();
}

void Main::StartDeleteConfirmation()
{
    if (!m_selection_mode || m_selected_count <= 0 || m_delete_dialog_open) {
        return;
    }

    snprintf(
        m_delete_message,
        sizeof(m_delete_message),
        "Delete selected PC%s?",
        m_selected_count == 1 ? "" : "s");

    SceMsgDialogParam param;
    SceMsgDialogUserMessageParam user_message;
    memset(&param, 0, sizeof(param));
    memset(&user_message, 0, sizeof(user_message));

    sceMsgDialogParamInit(&param);
    user_message.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL;
    user_message.msg = (const SceChar8 *)m_delete_message;
    user_message.buttonParam = NULL;
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user_message;

    int result = sceMsgDialogInit(&param);
    if (result < 0) {
        vita_debug_log(
            "[Main] delete confirmation init failed: 0x%08X",
            (unsigned int)result);
        return;
    }

    m_delete_dialog_open = true;
    if (!m_delete_dialog_task_registered) {
        paf::common::MainThreadCallList::Register(SelectionDialogPollTask, this);
        m_delete_dialog_task_registered = true;
    }
}

void Main::DeleteSelectedHosts()
{
    if (m_selected_count <= 0) {
        return;
    }

    bool failed = false;

    for (int i = 0; i < m_host_count; ++i) {
        if (!m_rows[i].selected) {
            continue;
        }

        if (MoonlightApp::Instance()->Hosts().Delete(m_hosts[i]) != 0) {
            failed = true;
        }
    }

    ExitSelectionMode();

    if (failed) {
        MoonlightApp::Instance()->Connection().ShowError("Unable to delete one or more PCs.");
    }
}

void Main::SelectionDialogPollTask(void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main) return;

    if (!main->m_delete_dialog_open) {
        if (main->m_delete_dialog_task_registered) {
            paf::common::MainThreadCallList::Unregister(SelectionDialogPollTask, main);
            main->m_delete_dialog_task_registered = false;
        }
        return;
    }

    if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return;
    }

    SceMsgDialogResult result;
    memset(&result, 0, sizeof(result));
    sceMsgDialogGetResult(&result);
    sceMsgDialogTerm();
    main->m_delete_dialog_open = false;

    if (main->m_delete_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(SelectionDialogPollTask, main);
        main->m_delete_dialog_task_registered = false;
    }

    if (result.buttonId == SCE_MSG_DIALOG_BUTTON_ID_OK) {
        main->DeleteSelectedHosts();
    }
}

void Main::OnSelectionCancel(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)self;
    (void)event;

    Main *main = (Main *)userdata;
    if (!main || main->m_delete_dialog_open) return;

    main->ExitSelectionMode();
    main->ShowIdleStatus();
}

void Main::OnSelectionAction(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)self;
    (void)event;

    Main *main = (Main *)userdata;
    if (!main || main->m_delete_dialog_open || main->m_selected_count <= 0) return;

    main->StartDeleteConfirmation();
}

void Main::OnSelectionSelectAll(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)self;
    (void)event;

    Main *main = (Main *)userdata;
    if (!main || main->m_delete_dialog_open) return;

    main->ToggleSelectAll();
}

void Main::ShowIdleStatus()
{
    if (m_host_count > 0 || m_selection_mode || m_connecting) {
        HideStatus();
    } else if (root) {
        paf::ui::Text *widget = static_cast<paf::ui::Text *>(
            root->FindChild("text_main_status"));
        const wchar_t *label = g_plugin ? g_plugin->GetString("msg_no_pcs") : NULL;
        if (widget && label) {
            widget->SetString(paf::wstring(label));
            widget->Show(paf::common::transition::Type_Reset);
        }
    }
}

void Main::HideStatus()
{
    if (!root) return;

    paf::ui::Text *widget = static_cast<paf::ui::Text *>(
        root->FindChild("text_main_status"));
    if (widget) {
        /* A parent transition must not expose a stale empty-list label. */
        widget->SetString(paf::wstring());
        widget->Hide(paf::common::transition::Type_Reset);
    }
}

void Main::OpenAddPc()
{
    if (Base::GetCurrent() != this || m_selection_mode || m_settings_suspended ||
        m_pending_action != Pending_None || m_connecting || m_ime_open || m_ime_retry_pending ||
        MoonlightApp::Instance()->Connection().IsBusy()) {
        return;
    }

    SuspendBackground();
    if (!MoonlightApp::Instance()->Discovery().IsIdle()) {
        m_pending_action = Pending_AddPc;
        if (root) root->SetActivate(false);
        return;
    }

    if (!m_ime_module_loaded) {
        int result = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        if (result < 0) {
            vita_debug_log(
                "[Main] failed to load SceIme: 0x%08X",
                (unsigned int)result);
            MoonlightApp::Instance()->Connection().ShowError("Unable to open the system keyboard.");
            return;
        }

        m_ime_module_loaded = true;
    }

    StartAddPcIme();
}

void Main::StartAddPcIme()
{
    if (m_ime_open || m_ime_retry_pending || !m_ime_module_loaded || m_connecting) {
        return;
    }

    memset(m_ime_input, 0, sizeof(m_ime_input));
    ConvertAsciiToImeText(
        m_add_pc_address[0] ? m_add_pc_address : "",
        m_ime_initial_text,
        sizeof(m_ime_initial_text) / sizeof(m_ime_initial_text[0]));

    sceImeDialogParamInit(&m_ime_param);
    m_ime_param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    m_ime_param.languagesForced = SCE_TRUE;
    m_ime_param.type = SCE_IME_TYPE_URL;
    m_ime_param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION |
                          SCE_IME_OPTION_NO_ASSISTANCE;
    m_ime_param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    m_ime_param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    m_ime_param.title = kImeTitle;
    m_ime_param.maxTextLength = kMaxHostAddressLength;
    m_ime_param.initialText = m_ime_initial_text;
    m_ime_param.inputTextBuffer = m_ime_input;
    m_ime_param.enterLabel = SCE_IME_ENTER_LABEL_GO;

    int result = sceImeDialogInit(&m_ime_param);
    if (result < 0) {
        if (result == SCE_COMMON_DIALOG_ERROR_BUSY) {
            RetryAddPcIme();
            return;
        }

        vita_debug_log(
            "[Main] sceImeDialogInit failed: 0x%08X",
            (unsigned int)result);
        MoonlightApp::Instance()->Connection().ShowError("Unable to open the system keyboard.");
        return;
    }

    m_ime_open = true;
    if (!m_ime_task_registered) {
        paf::common::MainThreadCallList::Register(ImePollTask, this);
        m_ime_task_registered = true;
    }
}

void Main::HandleAddPcImeResult()
{
    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));

    int get_result = sceImeDialogGetResult(&result);
    sceImeDialogTerm();
    m_ime_open = false;

    if (m_ime_task_registered) {
        paf::common::MainThreadCallList::Unregister(ImePollTask, this);
        m_ime_task_registered = false;
    }

    if (get_result < 0) {
        MoonlightApp::Instance()->Connection().ShowError("Unable to read the PC address.");
        return;
    }

    if (result.button == SCE_IME_DIALOG_BUTTON_CLOSE) {
        ShowIdleStatus();
        return;
    }

    if (result.button != SCE_IME_DIALOG_BUTTON_ENTER) {
        MoonlightApp::Instance()->Connection().ShowError("Enter PC hostname or IP address.");
        return;
    }

    char address[kMaxHostAddressLength + 1];
    if (!ConvertImeTextToAddress(m_ime_input, address, sizeof(address))) {
        MoonlightApp::Instance()->Connection().ShowError("Enter a valid PC address or host:port.");
        RetryAddPcIme();
        return;
    }

    strncpy(m_add_pc_address, address, sizeof(m_add_pc_address) - 1);
    m_add_pc_address[sizeof(m_add_pc_address) - 1] = '\0';
    StartManualConnection(m_add_pc_address);
}

void Main::StartManualConnection(const char *address)
{
    char host_address[256];
    uint16_t port;

    if (m_connecting || !address || !address[0]) {
        return;
    }

    if (!ParseHostPort(address, host_address, sizeof(host_address), &port)) {
        MoonlightApp::Instance()->Connection().ShowError("Enter a valid PC address or host:port.");
        RetryAddPcIme();
        return;
    }

    MoonlightHost host;
    memset(&host, 0, sizeof(host));
    host.port = port;
    strncpy(host.name, address, sizeof(host.name) - 1);
    host.name[sizeof(host.name) - 1] = '\0';
    strncpy(host.internal, host_address, sizeof(host.internal) - 1);
    host.internal[sizeof(host.internal) - 1] = '\0';

    m_connecting = true;
    SuspendBackground();

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_main"));
    if (list) {
        list->SetActivate(false);
    }

    HideStatus();

    int result = MoonlightApp::Instance()->Connection().Connect(host);
    if (result != 0) {
        m_connecting = false;
        if (list) {
            list->SetActivate(true);
        }
        ShowIdleStatus();
    }
}

void Main::RetryAddPcIme()
{
    m_ime_retry_pending = true;
    if (!m_ime_task_registered) {
        paf::common::MainThreadCallList::Register(ImePollTask, this);
        m_ime_task_registered = true;
    }
}

void Main::ImePollTask(void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main) {
        return;
    }

    if (main->m_ime_retry_pending && !main->m_ime_open) {
        if (MoonlightApp::Instance()->Connection().IsBusy()) return;
        main->m_ime_retry_pending = false;
        main->StartAddPcIme();
        return;
    }

    if (!main->m_ime_open) {
        if (main->m_ime_task_registered) {
            paf::common::MainThreadCallList::Unregister(ImePollTask, main);
            main->m_ime_task_registered = false;
        }
        return;
    }

    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return;
    }

    main->HandleAddPcImeResult();
}

void Main::OnMoonlightEvent(const MoonlightEvent *event, void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_HOST_AVAILABILITY_CHANGED:
        update_host_availability(static_cast<paf::ui::ListView *>(
            main->root->FindChild("list_view_main")), main->m_hosts);
        break;

    case MOONLIGHT_EVENT_DISCOVERY_CHANGED:
        if (main->CanDiscover() &&
            MoonlightApp::Instance()->Discovery().Accepts(*event)) main->RefreshHosts();
        break;

    case MOONLIGHT_EVENT_HOSTS_CHANGED:
        main->RefreshHosts();
        break;

    case MOONLIGHT_EVENT_CONNECTION_READY:
        main->m_connecting = false;
        {
            paf::ui::Widget *list = main->root->FindChild("list_view_main");
            if (list) list->SetActivate(true);
        }

        if (MoonlightApp::Instance()->Connection().State() ==
            MOONLIGHT_CONNECTION_PAIRED) {
            if (!Base::IsOpen("page_apps")) {
                Apps *apps = new Apps();
                if (!apps->IsValid()) {
                    delete apps;
                    MoonlightApp::Instance()->Connection().ShowError("Unable to open applications.");
                }
            }
            break;
        }

        if (MoonlightApp::Instance()->Connection().State() ==
            MOONLIGHT_CONNECTION_READY) {
            char pin[5] = {0};
            int prepare_result = MoonlightApp::Instance()->Pairing().Prepare(pin);
            if (prepare_result != 0) {
                MoonlightApp::Instance()->Connection().ShowError("Unable to prepare pairing.", prepare_result);
                return;
            }

            Pairing *pairing = new Pairing(pin);
            if (!pairing || !pairing->IsValid()) {
                delete pairing;
                MoonlightApp::Instance()->Connection().ShowError("Unable to open pairing screen.");
            }
            return;
        }

        MoonlightApp::Instance()->Connection().ShowError("Unable to connect to PC.");
        break;

    case MOONLIGHT_EVENT_CONNECTION_FAILED:
    case MOONLIGHT_EVENT_CONNECTION_CLOSED:
        main->m_connecting = false;
        main->RefreshHosts();
        {
            paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
                main->root->FindChild("list_view_main"));
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
        MoonlightApp::Instance()->Connection().ShowError("This PC needs pairing. Select it again to pair.");
        {
            paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
                main->root->FindChild("list_view_main"));
            if (list) list->SetActivate(true);
        }
        break;

    default:
        break;
    }
}

void Main::SuspendForSystemSettings()
{
    SuspendBackground();
    m_settings_suspended = true;
    m_pending_action = Pending_Settings;
    if (!root) return;
    root->SetActivate(false);
}

void Main::RestoreAfterSystemSettings()
{
    m_settings_suspended = false;
    if (!root) return;
    root->SetActivate(true);
}

}
