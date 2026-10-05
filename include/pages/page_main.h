#ifndef VITA_MOONLIGHT_PAGE_MAIN_H
#define VITA_MOONLIGHT_PAGE_MAIN_H

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
    void OpenAddHost();
    void RestoreEventCallback();

private:
    static void OnHostButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void InitialRefreshTask(void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);

    paf::ui::ListItem *CreateListItem(paf::ui::listview::ItemFactory::CreateParam &param);
    void RefreshHosts();
    void SelectHost(int index);
    void SetStatus(const char *text);

    MoonlightHost m_hosts[8];
    int m_host_count;
    int m_selected_index;
    bool m_connecting;
    int m_initial_refresh_attempts;
    HostButtonContext m_button_contexts[8];
};

}

#endif
