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

    enum SelectionAction {
        SelectionAction_None = 0,
        SelectionAction_Copy = 1,
        SelectionAction_Delete = 2
    };

    void EnterSelectionMode(int action);

private:
    static void OnHostButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void InitialRefreshTask(void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    static void ImePollTask(void *userdata);
    static void OnHostSelection(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnSelectionCancel(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnSelectionAction(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnSelectionSelectAll(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void SelectionDialogPollTask(void *userdata);

    paf::ui::ListItem *CreateListItem(paf::ui::listview::ItemFactory::CreateParam &param);
    void RefreshHosts();
    void SelectHost(int index);
    void SetStatus(const char *text);
    void StartAddPcIme();
    void HandleAddPcImeResult();
    void StartManualConnection(const char *address);
    void ExitSelectionMode();
    void ToggleHostSelection(int index);
    void ToggleSelectAll();
    void SetSelectionListLayout(bool selecting);
    void UpdateSelectionActionBar();
    void UpdateSelectionCount();
    void StartDeleteConfirmation();
    void DeleteSelectedHosts();
    void CopySelectedHosts();

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

    bool m_selection_mode;
    SelectionAction m_selection_action;
    bool m_selected_hosts[8];
    paf::ui::CheckBox *m_checkboxes[8];
    int m_selected_count;
    bool m_delete_dialog_open;
    bool m_delete_dialog_task_registered;
    char m_delete_message[512];
    bool m_clipboard_module_loaded;
};

}

#endif
