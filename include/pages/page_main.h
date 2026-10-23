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

    Main();
    virtual ~Main();

    static Main *Instance();
    void SuspendForSystemSettings();
    void RestoreAfterSystemSettings();
    void SuspendBackground();
    void OpenAddPc();
    void RestoreEventCallback();

    void EnterSelectionMode();
    bool HasHosts() const { return m_saved_count > 0; }

private:
    static void BackgroundTask(void *userdata);
    static void AvailabilityTask(void *userdata);
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
    bool CanDiscover() const;
    bool ReadHostRows(paf::vector<MoonlightHost> &hosts);
    void RefreshHosts();
    void UpdateHostItem(paf::ui::Widget *item, int index);
    void SelectHost(int index);
    void HideStatus();
    void ShowIdleStatus();
    void StartAddPcIme();
    void RetryAddPcIme();
    void HandleAddPcImeResult();
    void StartManualConnection(const char *address);
    void ExitSelectionMode();
    void ToggleHostSelection(int index);
    void ToggleSelectAll();
    bool AllHostsSelected() const;
    void UpdateSelectionActionBar();
    void UpdateSelectionCount();
    void StartDeleteConfirmation();
    void DeleteSelectedHosts();

    struct HostRowState {
        bool saved;
        bool selection_template;
        uint32_t token;
        uint32_t checkbox_token;
        bool selected;
    };
    int FindHostIndex(uint32_t token) const;

    paf::vector<MoonlightHost> m_hosts;
    paf::vector<MoonlightHost> m_saved_hosts;
    paf::vector<HostRowState> m_rows;
    uint32_t m_next_host_token;
    int m_host_count;
    int m_saved_count;
    enum PendingAction { Pending_None, Pending_AddPc, Pending_Settings };
    PendingAction m_pending_action;
    uint64_t m_discovery_retry_at;
    int m_discovery_error;
    uint64_t m_app_state_poll_at;
    bool m_system_ui_overlaid;
    bool m_list_ready;
    bool m_connecting;
    bool m_settings_suspended;
    int m_initial_refresh_attempts;
    int m_initial_refresh_stable_count;

    SceImeDialogParam m_ime_param;
    SceWChar16 m_ime_input[256];
    SceWChar16 m_ime_initial_text[256];
    char m_add_pc_address[256];
    bool m_ime_open;
    bool m_ime_task_registered;
    bool m_ime_retry_pending;
    bool m_ime_module_loaded;

    bool m_selection_mode;
    int m_selected_count;
    bool m_delete_dialog_open;
    bool m_delete_dialog_task_registered;
    char m_delete_message[512];
};

}

#endif
