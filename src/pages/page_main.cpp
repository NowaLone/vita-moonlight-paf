#include "pages/page_main.h"
#include "pages/page_search.h"
#include "pages/page_add_host.h"
#include "option_menu.h"
#include "common.h"
#include "moonlight/api.h"

namespace page {

static Main *s_main = NULL;

static void onSearch(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    if (!Base::IsOpen("page_search_pcs")) {
        Search *search = new Search();
        if (!search->IsValid()) {
            delete search;
        }
    }
}

static void onAdd(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    (void)userdata;
    if (!Base::IsOpen("page_add_manually")) {
        AddHost *addHost = new AddHost();
        if (!addHost->IsValid()) {
            delete addHost;
        }
    }
}

static void onOptionMenu(OptionMenu::EventType type, int button_index, void *userdata) {
    (void)userdata;
    if (type != OptionMenu::Event_Button || button_index != 0) return;

    Main *main = Main::Instance();
    if (main) {
        main->SuspendForSystemSettings();
    }

    if (moonlight_api_open_settings() != 0 && main) {
        main->RestoreAfterSystemSettings();
    }
}

static void onSettingsButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata) {
    (void)type;
    (void)self;
    (void)e;
    Main *main = (Main *)userdata;
    if (OptionMenu::Instance() != NULL) {
        return;
    }
    new OptionMenu(g_plugin, main != NULL ? main->root : NULL, onOptionMenu, NULL);
}

Main::Main()
    : Base("page_main", NULL, paf::Plugin::TransitionType_None, paf::Plugin::TransitionType_None) {
    s_main = this;
    if (root == NULL) {
        return;
    }
    bind_decide(root, "btn_search_pcs", onSearch);
    bind_decide(root, "btn_add_manually", onAdd);
    bind_decide(root, "settings_button", onSettingsButton, this);
}

Main::~Main() {
    if (s_main == this) {
        s_main = NULL;
    }
}

Main *Main::Instance() {
    return s_main;
}

void Main::SuspendForSystemSettings() {
    if (!root) return;
    root->SetActivate(false);
    root->Hide(paf::common::transition::Type_Reset);
}

void Main::RestoreAfterSystemSettings() {
    if (!root) return;
    root->Show(paf::common::transition::Type_Reset);
    root->SetActivate(true);
}

}
