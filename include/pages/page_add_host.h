#ifndef VITA_MOONLIGHT_PAGE_ADD_HOST_H
#define VITA_MOONLIGHT_PAGE_ADD_HOST_H

#include <psp2/ime_dialog.h>

#include "pages/page.h"

namespace page {

class AddHost : public Base {
public:
    AddHost();
    virtual ~AddHost();
    virtual Type GetType() { return Type_AddHost; }

    void RestoreEventCallback();

private:
    static void ImePollTask(void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);

    void StartIme();
    void HandleImeResult();
    void AddAndConnect(const char *address);
    void OnConnectionReady();
    void SetStatus(const char *text);

    SceImeDialogParam m_ime_param;
    SceWChar16 m_ime_input[256];
    SceWChar16 m_ime_initial_text[1];
    bool m_ime_open;
    bool m_ime_task_registered;
    bool m_ime_module_loaded;
    bool m_connecting;
};

}

#endif
