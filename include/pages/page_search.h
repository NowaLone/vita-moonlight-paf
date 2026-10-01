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
    void ContinuePairing();

    MoonlightHost m_hosts[8];
    int m_host_count;
    bool m_host_selected;
    bool m_pairing_pending;
    int m_selected_index;
    char m_pairing_pin[5];
    HostButtonContext m_button_contexts[8];
};

}

#endif
