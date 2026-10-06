#ifndef VITA_MOONLIGHT_PAGE_PAIRING_H
#define VITA_MOONLIGHT_PAGE_PAIRING_H

#include <stdint.h>

#include "pages/page.h"

namespace page {

class Pairing : public Base {
public:
    explicit Pairing(const char pin[5]);
    virtual ~Pairing();
    virtual Type GetType() { return Type_Pairing; }

    void RestoreEventCallback();

private:
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    static void OnDialogEvent(
        int32_t instanceSlot,
        CommonGuiDialog::DIALOG_CB buttonCode,
        void *userdata);

    void StartPairing();
    void ShowPairingError();
    void RetryPairing();
    void SetPin(const char pin[5]);
    void SetStatus(const char *text);

    char m_pin[5];
    bool m_pairing_started;
    int32_t m_dialog_slot;
};

}

#endif
