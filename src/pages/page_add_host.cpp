#include "pages/page_add_host.h"

namespace page {

AddHost::AddHost()
    : Base("page_add_manually", "btn_close_add_manual",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom) {
    if (!IsValid()) {
        return;
    }
}

AddHost::~AddHost() {}

}
