#include <paf.h>
#include <string.h>

#include "common.h"
#include "pages/page_pairing.h"
#include "pages/page_search.h"
#include "pages/page_apps.h"
#include "pages/page_main.h"
#include "app/moonlight_app.h"

namespace page {

Pairing::Pairing(const char pin[5])
    : Base("page_pairing", "btn_close_pairing",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom),
      m_pairing_started(false),
      m_dialog_open(false),
      m_dialog_task_registered(false)
{
    m_pin[0] = '\0';

    if (!IsValid()) {
        return;
    }

    SetPin(pin);

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    paf::ui::Widget *title = root->FindChild("text_top");
    if (title) {
        ((paf::ui::Text *)title)->SetString(
            paf::common::string_util::ToWString("Pair with PC"));
    }

    SetStatus("Enter this PIN on the PC.");
    StartPairing();
}

Pairing::~Pairing()
{
    if (m_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(DialogPollTask, this);
        m_dialog_task_registered = false;
    }

    if (m_dialog_open) {
        sceMsgDialogAbort();
        sceMsgDialogTerm();
        m_dialog_open = false;
    }

    if (!MoonlightApp::Instance() ||
        !MoonlightApp::Instance()->IsInitialized()) {
        return;
    }

    Search *search = static_cast<Search *>(Base::Find("page_search_pcs"));
    if (search) {
        search->RestoreEventCallback();
        return;
    }

    Main *main = Main::Instance();
    if (main) {
        main->RestoreEventCallback();
        return;
    }

    MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
}

void Pairing::RestoreEventCallback()
{
    if (MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    }
}

void Pairing::SetPin(const char pin[5])
{
    if (!pin) {
        m_pin[0] = '\0';
        return;
    }

    memcpy(m_pin, pin, sizeof(m_pin));

    if (root) {
        paf::ui::Widget *widget = root->FindChild("text_pairing_pin");
        if (widget) {
            paf::string pin_text = paf::common::FormatString(
                "%c  %c  %c  %c",
                m_pin[0],
                m_pin[1],
                m_pin[2],
                m_pin[3]);

            ((paf::ui::Text *)widget)->SetString(
                paf::common::string_util::ToWString(pin_text));
        }
    }
}

void Pairing::SetStatus(const char *text)
{
    if (!root || !text) return;

    paf::ui::Widget *widget = root->FindChild("text_pairing_status");
    if (!widget) return;

    ((paf::ui::Text *)widget)->SetString(
        paf::common::string_util::ToWString(text));
}

void Pairing::StartPairing()
{
    if (m_pairing_started || m_pin[0] == '\0') {
        return;
    }

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        SetStatus("Pairing unavailable.");
        return;
    }

    m_pairing_started = true;
    SetStatus("Pairing with PC...");

    int result = app->Pairing().Pair(m_pin);
    if (result != 0) {
        m_pairing_started = false;
        ShowPairingError();
    }
}

void Pairing::ShowPairingError()
{
    if (m_dialog_open) {
        return;
    }

    memset(&m_dialog_param, 0, sizeof(m_dialog_param));
    memset(&m_dialog_user, 0, sizeof(m_dialog_user));
    memset(&m_dialog_buttons, 0, sizeof(m_dialog_buttons));

    strncpy(m_dialog_title, "Pairing failed", sizeof(m_dialog_title) - 1);
    m_dialog_title[sizeof(m_dialog_title) - 1] = '\0';
    strncpy(m_dialog_message, "Incorrect PIN.", sizeof(m_dialog_message) - 1);
    m_dialog_message[sizeof(m_dialog_message) - 1] = '\0';

    sceMsgDialogParamInit(&m_dialog_param);

    m_dialog_buttons.msg1 = "OK";
    m_dialog_buttons.fontSize1 = SCE_MSG_DIALOG_FONT_SIZE_DEFAULT;

    m_dialog_user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    m_dialog_user.msg = (const SceChar8 *)m_dialog_message;
    m_dialog_user.buttonParam = &m_dialog_buttons;

    m_dialog_param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    m_dialog_param.userMsgParam = &m_dialog_user;

    if (sceMsgDialogInit(&m_dialog_param) < 0) {
        SetStatus("Pairing failed: unable to show error.");
        return;
    }

    m_dialog_open = true;
    if (!m_dialog_task_registered) {
        paf::common::MainThreadCallList::Register(DialogPollTask, this);
        m_dialog_task_registered = true;
    }
}

void Pairing::DialogPollTask(void *userdata)
{
    Pairing *pairing = (Pairing *)userdata;
    if (!pairing || !pairing->m_dialog_open) {
        return;
    }

    if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return;
    }

    SceMsgDialogResult result;
    memset(&result, 0, sizeof(result));
    sceMsgDialogGetResult(&result);
    sceMsgDialogTerm();

    pairing->m_dialog_open = false;
    if (pairing->m_dialog_task_registered) {
        paf::common::MainThreadCallList::Unregister(DialogPollTask, pairing);
        pairing->m_dialog_task_registered = false;
    }

    pairing->RetryPairing();
}

void Pairing::RetryPairing()
{
    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        SetStatus("Pairing unavailable.");
        return;
    }

    char pin[5] = {0};
    int result = app->Pairing().Prepare(pin);
    if (result != 0) {
        SetStatus("Unable to prepare pairing.");
        return;
    }

    SetPin(pin);
    StartPairing();
}

void Pairing::OnMoonlightEvent(
    const MoonlightEvent *event,
    void *userdata)
{
    Pairing *pairing = (Pairing *)userdata;
    if (!pairing || !event) {
        return;
    }

    switch (event->type) {
    case MOONLIGHT_EVENT_PAIRING_FINISHED:
        pairing->m_pairing_started = false;
        if (event->result == 0) {
            pairing->SetStatus("Paired. Loading applications...");

            if (!Base::IsOpen("page_apps")) {
                Apps *apps = new Apps();
                if (!apps->IsValid()) {
                    delete apps;
                    pairing->SetStatus("Unable to open applications.");
                }
            }
        } else {
            pairing->ShowPairingError();
        }
        break;

    case MOONLIGHT_EVENT_PAIRING_FAILED:
        pairing->m_pairing_started = false;
        pairing->ShowPairingError();
        break;

    case MOONLIGHT_EVENT_CONNECTION_FAILED:
        pairing->m_pairing_started = false;
        pairing->SetStatus("Connection failed.");
        break;

    default:
        break;
    }
}

}
