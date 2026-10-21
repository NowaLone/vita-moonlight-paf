#include <paf.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "pages/page_pairing.h"
#include "pages/page_search.h"
#include "pages/page_apps.h"
#include "pages/page_main.h"
#include "app/moonlight_app.h"
#include "moonlight/types.h"
#include "debug.h"

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

    if (root) {
        root->SetActivate(true);
    }

    SetPin(pin);

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    StartPairing();
}

Pairing::~Pairing()
{
    if (MoonlightApp::Instance()) {
        MoonlightApp::Instance()->Pairing().Cancel();
    }

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

void Pairing::StartPairing()
{
    if (m_pairing_started || m_pin[0] == '\0') {
        return;
    }

    MoonlightApp *app = MoonlightApp::Instance();
    if (!app) {
        ShowSystemError("Pairing unavailable.");
        return;
    }

    m_pairing_started = true;

    int result = app->Pairing().Pair(m_pin);
    if (result != 0) {
        m_pairing_started = false;
        ShowPairingError(MOONLIGHT_PAIRING_ERROR_INTERNAL);
    }
}

void Pairing::ShowPairingError(int result, int server_status_code,
                              const char *server_status_message)
{
    const char *message = "Unable to complete pairing.";

    switch (result) {
    case MOONLIGHT_PAIRING_ERROR_INCORRECT_PIN:
        message = "Incorrect PIN. Select the PC again to retry pairing.";
        break;

    case MOONLIGHT_PAIRING_ERROR_NETWORK:
        message = "Could not connect to the PC during pairing. Check the network connection and Sunshine.";
        break;

    case MOONLIGHT_PAIRING_ERROR_SECURITY:
        message = "Pairing failed security verification.";
        break;

    case MOONLIGHT_PAIRING_ERROR_PROTOCOL:
        message = "The PC rejected the pairing request.";
        break;

    case MOONLIGHT_PAIRING_ERROR_INTERNAL:
    default:
        break;
    }

    if (m_dialog_open) {
        return;
    }

    memset(&m_dialog_param, 0, sizeof(m_dialog_param));
    memset(&m_dialog_user, 0, sizeof(m_dialog_user));
    memset(&m_dialog_buttons, 0, sizeof(m_dialog_buttons));

    strncpy(m_dialog_title, "Pairing failed", sizeof(m_dialog_title) - 1);
    m_dialog_title[sizeof(m_dialog_title) - 1] = '\0';
    if (result == MOONLIGHT_PAIRING_ERROR_PROTOCOL && server_status_code > 0) {
        snprintf(m_dialog_message, sizeof(m_dialog_message),
                 "The PC returned a pairing error:\n%s (Error %d)",
                 server_status_message && server_status_message[0]
                     ? server_status_message : "The PC rejected the pairing request.",
                 server_status_code);
    } else {
        strncpy(m_dialog_message, message, sizeof(m_dialog_message) - 1);
    }
    m_dialog_message[sizeof(m_dialog_message) - 1] = '\0';

    ShowSystemError(m_dialog_message);
}

void Pairing::ShowSystemError(const char *message)
{
    vita_debug_log("[Pairing] error: %s", message);
    if (m_dialog_open) return;
    if (message != m_dialog_message) {
        strncpy(m_dialog_message, message, sizeof(m_dialog_message) - 1);
        m_dialog_message[sizeof(m_dialog_message) - 1] = '\0';
    }
    memset(&m_dialog_user, 0, sizeof(m_dialog_user));
    sceMsgDialogParamInit(&m_dialog_param);

    m_dialog_user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    m_dialog_user.msg = (const SceChar8 *)m_dialog_message;
    m_dialog_user.buttonParam = NULL;

    m_dialog_param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    m_dialog_param.userMsgParam = &m_dialog_user;

    int dialog_result = sceMsgDialogInit(&m_dialog_param);
    if (dialog_result < 0) {
        vita_debug_log("[Pairing] error dialog init failed result=0x%08X", (unsigned int)dialog_result);
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

    /*
     * A failed attempt is terminal. Acknowledging its error must not enqueue
     * another handshake or replace the PIN while the host finishes the old
     * attempt. Selecting the PC again explicitly starts a new attempt.
     */
    Base::CloseCurrent();
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
            if (!Base::IsOpen("page_apps")) {
                Apps *apps = new Apps();
                if (!apps->IsValid()) {
                    delete apps;
                    pairing->ShowSystemError("Unable to open applications.");
                    return;
                }

                /*
                 * Pairing is complete, so it must not remain a Back target.
                 * Its destructor restores the parent callback; hand events
                 * back to Apps after removing it from the existing stack.
                 */
                delete pairing;
                apps->RestoreEventCallback();
                return;
            }
        } else {
            pairing->ShowPairingError(event->result, event->server_status_code,
                                      event->server_status_message);
        }
        break;

    case MOONLIGHT_EVENT_PAIRING_FAILED:
        pairing->m_pairing_started = false;
        pairing->ShowPairingError(event->result, event->server_status_code,
                                  event->server_status_message);
        break;

    case MOONLIGHT_EVENT_CONNECTION_FAILED:
        pairing->m_pairing_started = false;
        pairing->ShowPairingError(MOONLIGHT_PAIRING_ERROR_NETWORK);
        break;

    default:
        break;
    }
}

}
