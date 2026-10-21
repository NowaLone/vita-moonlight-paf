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
    static void OnHostButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    void RefreshHosts();
    void ShowEmptyState();
    void HideStatus();
    void SelectHost(int index);
    void OnConnectionReady();

    int FindHostIndex(uint32_t token) const;
    paf::vector<MoonlightHost> m_hosts;
    paf::vector<uint32_t> m_host_tokens;
    uint32_t m_next_host_token;
    int m_host_count;
    bool m_host_selected;
    int m_selected_index;
    paf::ui::ListView *m_list;

};

}

#endif
