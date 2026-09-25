#ifndef VITA_MOONLIGHT_PAGE_SEARCH_H
#define VITA_MOONLIGHT_PAGE_SEARCH_H

#include "pages/page.h"

namespace page {

class Search : public Base {
public:
    Search();
    virtual ~Search();
    virtual Type GetType() { return Type_Search; }
};

}

#endif
