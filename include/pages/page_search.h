#ifndef VITA_MOONLIGHT_PAGE_SEARCH_H
#define VITA_MOONLIGHT_PAGE_SEARCH_H

#include "pages/page.h"
#include "moonlight/types.h"

namespace page {

class Search : public Base {
public:
    class ListViewFactory : public paf::ui::listview::ItemFactory {
    public:
        explicit ListViewFactory(Search *parent) : m_parent(parent) {}
        virtual paf::ui::ListItem *Create(CreateParam &param) {
            return m_parent->CreateListItem(param);
        }
    private:
        Search *m_parent;
    };

    Search();
    virtual ~Search();
    virtual Type GetType() { return Type_Search; }

    void RestoreEventCallback();
    paf::ui::ListItem *CreateListItem(paf::ui::listview::ItemFactory::CreateParam &param);

private:
    struct HostButtonContext {
        Search *page;
        int index;
    };

    static void OnHostButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    void RefreshHosts();
    void SetStatus(const char *text);
    void SelectHost(int index);
    void OnConnectionReady();

    MoonlightHost m_hosts[8];
    int m_host_count;
    bool m_host_selected;
    int m_selected_index;
    HostButtonContext m_button_contexts[8];
    paf::ui::ListView *m_list;

};

}

#endif
