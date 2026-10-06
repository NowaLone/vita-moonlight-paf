#ifndef VITA_MOONLIGHT_PAGE_MAIN_H
#define VITA_MOONLIGHT_PAGE_MAIN_H

#include <psp2/ime_dialog.h>

#include "pages/page.h"
#include "moonlight/types.h"

namespace page {

class Main : public Base {
public:
    class ListViewFactory : public paf::ui::listview::ItemFactory {
    public:
        explicit ListViewFactory(Main *parent) : m_parent(parent) {}
        virtual paf::ui::ListItem *Create(CreateParam &param) {
            return m_parent->CreateListItem(param);
        }
    private:
        Main *m_parent;
    };

    struct HostButtonContext {
        Main *page;
        int index;
    };

    Main();
    virtual ~Main();
    virtual Type GetType() { return Type_Main; }

    static Main *Instance();
    void SuspendForSystemSettings();
    void RestoreAfterSystemSettings();
    void OpenSearch();
    void OpenAddPc();
    void RestoreEventCallback();

private:
    static void OnHostButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void InitialRefreshTask(void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    static void ImePollTask(void *userdata);

    paf::ui::ListItem *CreateListItem(paf::ui::listview::ItemFactory::CreateParam &param);
    void RefreshHosts();
    void SelectHost(int index);
    void SetStatus(const char *text);
    void StartAddPcIme();
    void HandleAddPcImeResult();
    void StartManualConnection(const char *address);
    void SetAddPcLabel(const char *text);

    MoonlightHost m_hosts[8];
    int m_host_count;
    int m_selected_index;
    bool m_connecting;
    int m_initial_refresh_attempts;
    HostButtonContext m_button_contexts[8];

    SceImeDialogParam m_ime_param;
    SceWChar16 m_ime_input[256];
    SceWChar16 m_ime_initial_text[256];
    char m_add_pc_address[256];
    bool m_ime_open;
    bool m_ime_task_registered;
    bool m_ime_retry_pending;
    bool m_ime_module_loaded;
};

}

#endif
