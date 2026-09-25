#ifndef VITA_MOONLIGHT_PAGE_ADD_HOST_H
#define VITA_MOONLIGHT_PAGE_ADD_HOST_H

#include "pages/page.h"

namespace page {

class AddHost : public Base {
public:
    AddHost();
    virtual ~AddHost();
    virtual Type GetType() { return Type_AddHost; }
};

}

#endif
