#ifndef VITA_MOONLIGHT_PAGE_SEARCH_H
#define VITA_MOONLIGHT_PAGE_SEARCH_H

#include "pages/page.h"
#include "moonlight/types.h"

namespace page {

class Search : public Base {
public:
    Search();
    virtual ~Search();
    virtual Type GetType() { return Type_Search; }

private:
    struct HostButtonContext {
        Search *page;
        int index;
    };

    static void OnHostButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    void RefreshHosts();
    void SetStatus(const char *text);
    void SetHostButton(int index, const MoonlightHost &host);
    void SelectHost(int index);

    MoonlightHost m_hosts[8];
    int m_host_count;
    bool m_host_selected;
    HostButtonContext m_button_contexts[8];
};

}

#endif
