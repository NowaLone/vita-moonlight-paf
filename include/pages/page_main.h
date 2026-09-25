#ifndef VITA_MOONLIGHT_PAGE_MAIN_H
#define VITA_MOONLIGHT_PAGE_MAIN_H

#include "pages/page.h"

namespace page {

class Main : public Base {
public:
    Main();
    virtual ~Main();
    virtual Type GetType() { return Type_Main; }

    static Main *Instance();
};

}

#endif
