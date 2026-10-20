#ifndef VITA_MOONLIGHT_PAGE_PAIRING_H
#define VITA_MOONLIGHT_PAGE_PAIRING_H

#include <stdint.h>

#include "pages/page.h"
#include "moonlight/types.h"
#include <psp2/message_dialog.h>

namespace page {

class Pairing : public Base {
public:
    explicit Pairing(const char pin[5]);
    virtual ~Pairing();
    virtual Type GetType() { return Type_Pairing; }

    void RestoreEventCallback();

private:
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    static void DialogPollTask(void *userdata);

    void StartPairing();
    void ShowPairingError(int result, int server_status_code = 0,
                          const char *server_status_message = NULL);
    void ShowSystemError(const char *message);
    void SetPin(const char pin[5]);

    char m_pin[5];
    bool m_pairing_started;
    bool m_dialog_open;
    bool m_dialog_task_registered;
    SceMsgDialogParam m_dialog_param;
    SceMsgDialogUserMessageParam m_dialog_user;
    SceMsgDialogButtonsParam m_dialog_buttons;
    char m_dialog_title[128];
    char m_dialog_message[512];
};

}

#endif
