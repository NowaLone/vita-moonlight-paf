#include <paf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <psp2/common_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/sysmodule.h>

#include "pages/page_main.h"
#include "pages/page_search.h"
#include "pages/page_apps.h"
#include "pages/page_pairing.h"
#include "option_menu.h"
#include "common.h"
#include "app/moonlight_app.h"
#include "debug.h"

namespace page {

static Main *s_main = NULL;

extern "C" int sceClipboardSetText(const SceWChar16 *text);

namespace {

static const int kGameStreamPort = 47989;
static const int kMaxHostAddressLength = 255;
static const int kMaxClipboardTextLength = 2047;

static const SceWChar16 kImeTitle[] = {
    'P', 'C', ' ', 'a', 'd', 'd', 'r', 'e', 's', 's', 0
};

static void CopyImeText(
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

static bool CopyImeAddress(
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
        if (MoonlightApp::Instance()->Settings().Open() != 0) {
            main->RestoreAfterSystemSettings();
        }
        break;

    case OptionMenu::Button_Copy:
        main->EnterSelectionMode(Main::SelectionAction_Copy);
        break;

    case OptionMenu::Button_Delete:
        main->EnterSelectionMode(Main::SelectionAction_Delete);
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
      m_initial_refresh_attempts(0),
      m_ime_open(false),
      m_ime_task_registered(false),
      m_ime_retry_pending(false),
      m_ime_module_loaded(false),
      m_selection_mode(false),
      m_selection_action(SelectionAction_None),
      m_selected_count(0),
      m_delete_dialog_open(false),
      m_delete_dialog_task_registered(false),
      m_clipboard_module_loaded(false)
{
    memset(&m_ime_param, 0, sizeof(m_ime_param));
    memset(m_ime_input, 0, sizeof(m_ime_input));
    memset(m_ime_initial_text, 0, sizeof(m_ime_initial_text));
    memset(m_add_pc_address, 0, sizeof(m_add_pc_address));
    memset(m_selected_hosts, 0, sizeof(m_selected_hosts));
    memset(m_delete_message, 0, sizeof(m_delete_message));

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
    bind_decide(root, "btn_add_pc", onAdd, this);
    bind_decide(root, "settings_button", onSettingsButton, this);
    bind_decide(root, "btn_selection_cancel", OnSelectionCancel, this);
    bind_decide(root, "btn_selection_action", OnSelectionAction, this);

    root->FindChild("plane_main_selection_actions")->Hide(
        paf::common::transition::Type_Reset);

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

    if (m_clipboard_module_loaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_CLIPBOARD);
        m_clipboard_module_loaded = false;
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
    button->SetActivate(!m_selection_mode);

    paf::ui::Widget *checkbox_widget = item->FindChild("checkbox_list_item");
    if (checkbox_widget) {
        paf::ui::CheckBox *checkbox = (paf::ui::CheckBox *)checkbox_widget;
        checkbox->SetName((uint32_t)param.cell_index);
        checkbox->SetCheck(
            m_selection_mode ? m_selected_hosts[param.cell_index] : false,
            0.0f,
            false);
        checkbox->AddEventCallback(
            paf::ui::CheckBox::CB_BTN_DECIDE,
            OnHostSelection,
            &m_button_contexts[param.cell_index]);

        if (m_selection_mode) {
            checkbox->Show(paf::common::transition::Type_Reset);
            checkbox->SetActivate(true);
        } else {
            checkbox->Hide(paf::common::transition::Type_Reset);
            checkbox->SetActivate(false);
        }
    }

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

    if (context->page->m_selection_mode) {
        return;
    }

    context->page->SelectHost(context->index);
}

void Main::OnHostSelection(
    int32_t type,
    paf::ui::Handler *self,
    paf::ui::Event *event,
    void *userdata)
{
    (void)type;
    (void)event;

    HostButtonContext *context = (HostButtonContext *)userdata;
    if (!context || !context->page || !self ||
        context->index < 0 || context->index >= context->page->m_host_count ||
        !context->page->m_selection_mode) {
        return;
    }

    paf::ui::CheckBox *checkbox = (paf::ui::CheckBox *)self;
    bool checked = checkbox->IsChecked();
    if (context->page->m_selected_hosts[context->index] != checked) {
        context->page->m_selected_hosts[context->index] = checked;
        context->page->UpdateSelectionCount();
    }
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

void Main::UpdateSelectionCount()
{
    m_selected_count = 0;
    for (int i = 0; i < m_host_count; ++i) {
        if (m_selected_hosts[i]) {
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
        action->SetString(paf::common::string_util::ToWString(
            m_selection_action == SelectionAction_Copy ? "Copy" : "Delete"));
        action->SetActivate(m_selected_count > 0);
    }
}

void Main::EnterSelectionMode(int action)
{
    if (m_connecting || m_ime_open || m_ime_retry_pending ||
        m_host_count <= 0 ||
        (action != SelectionAction_Copy && action != SelectionAction_Delete)) {
        return;
    }

    m_selection_mode = true;
    m_selection_action = (SelectionAction)action;
    m_selected_count = 0;
    memset(m_selected_hosts, 0, sizeof(m_selected_hosts));

    paf::ui::Widget *normal_actions = root->FindChild("plane_main_actions");
    paf::ui::Widget *status = root->FindChild("text_main_status");
    paf::ui::Widget *settings = root->FindChild("settings_button");
    paf::ui::Widget *selection_actions = root->FindChild("plane_main_selection_actions");

    if (normal_actions) normal_actions->Hide(paf::common::transition::Type_Reset);
    if (status) status->Hide(paf::common::transition::Type_Reset);
    if (settings) settings->Hide(paf::common::transition::Type_Reset);
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
    m_selection_action = SelectionAction_None;
    m_selected_count = 0;
    memset(m_selected_hosts, 0, sizeof(m_selected_hosts));

    paf::ui::Widget *normal_actions = root->FindChild("plane_main_actions");
    paf::ui::Widget *status = root->FindChild("text_main_status");
    paf::ui::Widget *settings = root->FindChild("settings_button");
    paf::ui::Widget *selection_actions = root->FindChild("plane_main_selection_actions");

    if (selection_actions) selection_actions->Hide(paf::common::transition::Type_Reset);
    if (normal_actions) normal_actions->Show(paf::common::transition::Type_Reset);
    if (status) status->Show(paf::common::transition::Type_Reset);
    if (settings) settings->Show(paf::common::transition::Type_Reset);

    RefreshHosts();
}

void Main::StartDeleteConfirmation()
{
    if (!m_selection_mode || m_selection_action != SelectionAction_Delete ||
        m_selected_count <= 0 || m_delete_dialog_open) {
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
    user_message.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_YESNO;
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

    int selected_count = m_selected_count;
    int deleted_count = 0;
    bool failed = false;

    for (int i = 0; i < m_host_count; ++i) {
        if (!m_selected_hosts[i]) {
            continue;
        }

        if (MoonlightApp::Instance()->Hosts().Delete(m_hosts[i]) == 0) {
            ++deleted_count;
        } else {
            failed = true;
        }
    }

    ExitSelectionMode();

    if (failed) {
        SetStatus("Unable to delete one or more PCs.");
    } else {
        paf::string status = paf::common::FormatString(
            "Deleted %d PC%s.",
            deleted_count,
            selected_count == 1 ? "" : "s");
        SetStatus(status.c_str());
    }
}

void Main::CopySelectedHosts()
{
    if (m_selected_count <= 0) {
        return;
    }

    if (!m_clipboard_module_loaded) {
        int result = sceSysmoduleLoadModule(SCE_SYSMODULE_CLIPBOARD);
        if (result < 0) {
            vita_debug_log(
                "[Main] failed to load SceClipboard: 0x%08X",
                (unsigned int)result);
            ExitSelectionMode();
            SetStatus("Unable to open the system clipboard.");
            return;
        }

        m_clipboard_module_loaded = true;
    }

    char text[2048];
    size_t length = 0;
    text[0] = '\0';

    for (int i = 0; i < m_host_count; ++i) {
        if (!m_selected_hosts[i]) {
            continue;
        }

        const char *address = m_hosts[i].internal;
        if (!address[0]) {
            address = m_hosts[i].external;
        }
        if (!address || !address[0]) {
            continue;
        }

        int written = snprintf(
            text + length,
            sizeof(text) - length,
            "%s%s%u",
            length > 0 ? "\n" : "",
            address,
            m_hosts[i].port != 0 && m_hosts[i].port != kGameStreamPort
                ? (unsigned int)m_hosts[i].port
                : 0u);

        if (written < 0 || (size_t)written >= sizeof(text) - length) {
            break;
        }

        if (m_hosts[i].port != 0 && m_hosts[i].port != kGameStreamPort) {
            /* The formatted string already needs the colon before the port. */
            size_t current = length + (size_t)written;
            size_t address_end = current;
            while (address_end > length && text[address_end - 1] >= '0' && text[address_end - 1] <= '9') {
                --address_end;
            }
            if (address_end == current || address_end == length) {
                continue;
            }
        }

        length += (size_t)written;
    }

    /* Rebuild the clipboard text with the unambiguous host[:port] syntax. */
    length = 0;
    text[0] = '\0';
    for (int i = 0; i < m_host_count; ++i) {
        if (!m_selected_hosts[i]) continue;
        const char *address = m_hosts[i].internal[0]
            ? m_hosts[i].internal
            : m_hosts[i].external;
        if (!address || !address[0]) continue;

        int written = snprintf(
            text + length,
            sizeof(text) - length,
            "%s%s%s",
            length > 0 ? "\n" : "",
            address,
            "");
        if (written < 0 || (size_t)written >= sizeof(text) - length) break;
        length += (size_t)written;

        if (m_hosts[i].port != 0 && m_hosts[i].port != kGameStreamPort) {
            int port_written = snprintf(
                text + length,
                sizeof(text) - length,
                ":%u",
                (unsigned int)m_hosts[i].port);
            if (port_written < 0 || (size_t)port_written >= sizeof(text) - length) break;
            length += (size_t)port_written;
        }
    }

    if (length == 0 || length > (size_t)kMaxClipboardTextLength) {
        ExitSelectionMode();
        SetStatus("Nothing to copy.");
        return;
    }

    SceWChar16 wide_text[2048];
    CopyImeText(text, wide_text, sizeof(wide_text) / sizeof(wide_text[0]));
    int result = sceClipboardSetText(wide_text);
    ExitSelectionMode();

    if (result < 0) {
        vita_debug_log(
            "[Main] sceClipboardSetText failed: 0x%08X",
            (unsigned int)result);
        SetStatus("Unable to copy selected PCs.");
        return;
    }

    SetStatus("Copied selected PC addresses.");
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

    if (result.buttonId == SCE_MSG_DIALOG_BUTTON_ID_YES) {
        main->DeleteSelectedHosts();
    } else {
        main->SetStatus("Select PCs to delete.");
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
    main->SetStatus("Select a PC to connect.");
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

    if (main->m_selection_action == SelectionAction_Copy) {
        main->CopySelectedHosts();
    } else if (main->m_selection_action == SelectionAction_Delete) {
        main->StartDeleteConfirmation();
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

void Main::OpenAddPc()
{
    if (m_connecting || m_ime_open || m_ime_retry_pending) {
        return;
    }

    if (!m_ime_module_loaded) {
        int result = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        if (result < 0) {
            vita_debug_log(
                "[Main] failed to load SceIme: 0x%08X",
                (unsigned int)result);
            SetStatus("Unable to open the system keyboard.");
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
    CopyImeText(
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
            m_ime_retry_pending = true;
            SetStatus("Opening system keyboard...");
            if (!m_ime_task_registered) {
                paf::common::MainThreadCallList::Register(ImePollTask, this);
                m_ime_task_registered = true;
            }
            return;
        }

        vita_debug_log(
            "[Main] sceImeDialogInit failed: 0x%08X",
            (unsigned int)result);
        SetStatus("Unable to open the system keyboard.");
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

    if (sceImeDialogGetResult(&result) < 0) {
        sceImeDialogTerm();
        m_ime_open = false;
        if (m_ime_task_registered) {
            paf::common::MainThreadCallList::Unregister(ImePollTask, this);
            m_ime_task_registered = false;
        }
        SetStatus("Unable to read the PC address.");
        return;
    }

    sceImeDialogTerm();
    m_ime_open = false;

    if (m_ime_task_registered) {
        paf::common::MainThreadCallList::Unregister(ImePollTask, this);
        m_ime_task_registered = false;
    }

    if (result.button == SCE_IME_DIALOG_BUTTON_CLOSE) {
        SetStatus("Select a PC to connect.");
        return;
    }

    if (result.button != SCE_IME_DIALOG_BUTTON_ENTER) {
        SetStatus("Enter PC hostname or IP address.");
        return;
    }

    char address[kMaxHostAddressLength + 1];
    if (!CopyImeAddress(m_ime_input, address, sizeof(address))) {
        SetStatus("Enter a valid PC address or host:port.");
        StartAddPcIme();
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
        SetStatus("Enter a valid PC address or host:port.");
        StartAddPcIme();
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

    paf::ui::ListView *list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_generic"));
    if (list) {
        list->SetActivate(false);
    }

    SetStatus("Connecting to PC...");

    int result = MoonlightApp::Instance()->Connection().Connect(host);
    if (result != 0) {
        m_connecting = false;
        if (list) {
            list->SetActivate(true);
        }
        SetStatus("Unable to start connection.");
    }
}

void Main::ImePollTask(void *userdata)
{
    Main *main = (Main *)userdata;
    if (!main) {
        return;
    }

    if (main->m_ime_retry_pending && !main->m_ime_open) {
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
