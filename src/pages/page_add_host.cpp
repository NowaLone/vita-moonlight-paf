#include "pages/page_add_host.h"
#include "common.h"
namespace page {

static void onBack(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    Base::CloseType(Type_AddHost);
}

AddHost::AddHost()
    : Base("page_add_manually", "btn_close_add_manual",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom) {
    if (!IsValid()) {
        return;
    }
    bind_decide(root, "btn_close_add_manual", onBack);
}

AddHost::~AddHost() {}

}
