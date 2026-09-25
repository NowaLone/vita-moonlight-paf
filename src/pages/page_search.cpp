#include "pages/page_search.h"
#include "common.h"
#include "moonlight/api.h"

namespace page {

static void onBack(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    Base::CloseType(Type_Search);
}

Search::Search()
    : Base("page_search_pcs", "btn_close_search",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom) {
    bind_decide(root, "btn_close_search", onBack);
    moonlight_api_search_hosts();
}

Search::~Search() {}

}
