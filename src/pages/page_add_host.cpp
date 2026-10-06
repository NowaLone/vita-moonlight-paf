#include <paf.h>
#include <string.h>

#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <psp2/sysmodule.h>

#include "pages/page_add_host.h"
#include "pages/page_apps.h"
#include "pages/page_pairing.h"
#include "pages/page_main.h"
#include "app/moonlight_app.h"
#include "debug.h"

namespace page {

namespace {

static const int kGameStreamPort = 47989;
static const int kMaxHostAddressLength = 255;

static const SceWChar16 kImeTitle[] = {
    'P', 'C', ' ', 'a', 'd', 'd', 'r', 'e', 's', 's', 0
};

static bool CopyImeAddress(const SceWChar16 *input, char *output, size_t output_size)
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

AddHost::AddHost()
    : Base("page_add_manually", "btn_close_add_manual",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom),
      m_ime_open(false),
      m_ime_task_registered(false),
      m_ime_retry_pending(false),
      m_ime_module_loaded(false),
      m_connecting(false)
{
    memset(m_ime_input, 0, sizeof(m_ime_input));
    memset(m_ime_initial_text, 0, sizeof(m_ime_initial_text));
    memset(&m_ime_param, 0, sizeof(m_ime_param));

    if (!IsValid()) {
        return;
    }

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    paf::ui::Widget *title = root->FindChild("text_add_manual_title");
    if (title) {
        ((paf::ui::Text *)title)->SetString(
            paf::common::string_util::ToWString("Add PC"));
    }

    SetStatus("Enter a PC hostname or IP address.");

    int result = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    if (result < 0) {
        vita_debug_log(
            "[AddHost] failed to load SceIme: 0x%08X",
            (unsigned int)result);
        SetStatus("Unable to open the system keyboard.");
        return;
    }

    m_ime_module_loaded = true;
    StartIme();
}

AddHost::~AddHost()
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

    if (!MoonlightApp::Instance() ||
        !MoonlightApp::Instance()->IsInitialized()) {
        return;
    }

    Main *main = Main::Instance();
    if (main) {
        main->RestoreEventCallback();
        return;
    }

    MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
}

void AddHost::RestoreEventCallback()
{
    if (MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    }
}

void AddHost::SetStatus(const char *text)
{
    if (!root || !text) {
        return;
    }

    paf::ui::Widget *widget = root->FindChild("text_add_manual_status");
    if (!widget) {
        return;
    }

    ((paf::ui::Text *)widget)->SetString(
        paf::common::string_util::ToWString(text));
}

void AddHost::StartIme()
{
    if (m_ime_open || !m_ime_module_loaded || m_connecting) {
        return;
    }

    memset(m_ime_input, 0, sizeof(m_ime_input));

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
        vita_debug_log(
            "[AddHost] sceImeDialogInit failed: 0x%08X",
            (unsigned int)result);

        if (result == SCE_COMMON_DIALOG_ERROR_BUSY) {
            m_ime_retry_pending = true;
            SetStatus("Opening system keyboard...");
            if (!m_ime_task_registered) {
                paf::common::MainThreadCallList::Register(ImePollTask, this);
                m_ime_task_registered = true;
            }
            return;
        }

        m_ime_retry_pending = false;
        SetStatus("Unable to open the system keyboard.");
        if (m_ime_task_registered) {
            paf::common::MainThreadCallList::Unregister(ImePollTask, this);
            m_ime_task_registered = false;
        }
        return;
    }

    m_ime_retry_pending = false;
    m_ime_open = true;
    if (!m_ime_task_registered) {
        paf::common::MainThreadCallList::Register(ImePollTask, this);
        m_ime_task_registered = true;
    }
}

void AddHost::HandleImeResult()
{
    m_ime_retry_pending = false;

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
        Base::CloseCurrent();
        return;
    }

    if (result.button != SCE_IME_DIALOG_BUTTON_ENTER) {
        SetStatus("Enter a PC hostname or IP address.");
        StartIme();
        return;
    }

    char address[kMaxHostAddressLength + 1];
    if (!CopyImeAddress(m_ime_input, address, sizeof(address))) {
        SetStatus("Enter a valid PC hostname or IP address.");
        StartIme();
        return;
    }

    AddAndConnect(address);
}

void AddHost::ImePollTask(void *userdata)
{
    AddHost *page = (AddHost *)userdata;
    if (!page) {
        return;
    }

    if (page->m_ime_retry_pending && !page->m_ime_open) {
        page->StartIme();
        return;
    }

    if (!page->m_ime_open) {
        if (page->m_ime_task_registered) {
            paf::common::MainThreadCallList::Unregister(ImePollTask, page);
            page->m_ime_task_registered = false;
        }
        return;
    }

    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return;
    }

    page->HandleImeResult();
}

void AddHost::AddAndConnect(const char *address)
{
    if (m_connecting || !address || !address[0]) {
        return;
    }

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        SetStatus("Unable to connect to PC.");
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
    if (root) {
        root->SetActivate(false);
    }
    SetStatus("Connecting to PC...");

    int connect_result = app->Connection().Connect(host);
    if (connect_result != 0) {
        m_connecting = false;
        if (root) {
            root->SetActivate(true);
        }
        vita_debug_log(
            "[AddHost] connection enqueue failed: 0x%08X",
            (unsigned int)connect_result);
        SetStatus("Unable to start connection.");
        StartIme();
    }
}

void AddHost::OnConnectionReady()
{
    MoonlightApp *app = MoonlightApp::Instance();
    if (!app || !m_connecting) {
        return;
    }

    m_connecting = false;

    if (app->Connection().State() == MOONLIGHT_CONNECTION_PAIRED) {
        vita_debug_log("[AddHost] PC already paired, opening Apps");
        Base::CloseType(Type_AddHost);
        Apps *apps = new Apps();
        if (!apps || !apps->IsValid()) {
            delete apps;
        }
        return;
    }

    char pin[5] = {0};
    int prepare_result = app->Pairing().Prepare(pin);
    if (prepare_result != 0) {
        if (root) {
            root->SetActivate(true);
        }
        SetStatus("Unable to prepare pairing.");
        return;
    }

    vita_debug_log("[AddHost] PC needs pairing, opening Pairing page");
    Base::CloseType(Type_AddHost);
    Pairing *pairing = new Pairing(pin);
    if (!pairing || !pairing->IsValid()) {
        delete pairing;
    }
}

void AddHost::OnMoonlightEvent(const MoonlightEvent *event, void *userdata)
{
    AddHost *page = (AddHost *)userdata;
    if (!page || !event) {
        return;
    }

    switch (event->type) {
    case MOONLIGHT_EVENT_CONNECTION_READY:
        page->OnConnectionReady();
        break;

    case MOONLIGHT_EVENT_CONNECTION_FAILED:
        page->m_connecting = false;
        if (page->root) {
            page->root->SetActivate(true);
        }
        page->SetStatus("Unable to connect to PC.");
        break;

    default:
        break;
    }
}

}
