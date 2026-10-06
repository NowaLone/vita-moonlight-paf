#include <paf.h>
#include <string.h>

#include <psp2/common_dialog.h>
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

namespace {

static const int kGameStreamPort = 47989;
static const int kMaxHostAddressLength = 255;

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
      m_initial_refresh_attempts(0),
      m_ime_open(false),
      m_ime_task_registered(false),
      m_ime_retry_pending(false),
      m_ime_module_loaded(false)
{
    memset(&m_ime_param, 0, sizeof(m_ime_param));
    memset(m_ime_input, 0, sizeof(m_ime_input));
    memset(m_ime_initial_text, 0, sizeof(m_ime_initial_text));
    memset(m_add_pc_address, 0, sizeof(m_add_pc_address));

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
        SetStatus("Enter a valid PC hostname or IP address.");
        StartAddPcIme();
        return;
    }

    strncpy(m_add_pc_address, address, sizeof(m_add_pc_address) - 1);
    m_add_pc_address[sizeof(m_add_pc_address) - 1] = '\0';
    StartManualConnection(m_add_pc_address);
}

void Main::StartManualConnection(const char *address)
{
    if (m_connecting || !address || !address[0]) {
        return;
    }

    MoonlightHost host;
    memset(&host, 0, sizeof(host));
    host.port = kGameStreamPort;
    strncpy(host.name, address, sizeof(host.name) - 1);
    host.name[sizeof(host.name) - 1] = '\0';
    strncpy(host.internal, address, sizeof(host.internal) - 1);
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
