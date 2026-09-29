#include "pages/page_search.h"
#include "app/moonlight_app.h"

namespace page {

Search::Search()
    : Base("page_search_pcs", "btn_close_search",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom) {
    if (!IsValid()) {
        return;
    }
    MoonlightApp::Instance()->Hosts().Search();
}

Search::~Search() {}

}
