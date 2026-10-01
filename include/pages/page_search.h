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
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    void RefreshHosts();
    void SetStatus(const char *text);
    void SetHostButton(int index, const MoonlightHost &host);

    MoonlightHost m_hosts[8];
    int m_host_count;
};

}

#endif
