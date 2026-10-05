#ifndef VITA_MOONLIGHT_PAGE_APPS_H
#define VITA_MOONLIGHT_PAGE_APPS_H

#include "pages/page.h"
#include "moonlight/types.h"

namespace page {

class Apps : public Base {
public:
    class ListViewFactory : public paf::ui::listview::ItemFactory {
    public:
        explicit ListViewFactory(Apps *parent) : m_parent(parent) {}
        virtual paf::ui::ListItem *Create(CreateParam &param) {
            return m_parent->CreateListItem(param);
        }
    private:
        Apps *m_parent;
    };

    Apps();
    virtual ~Apps();
    virtual Type GetType() { return Type_Apps; }

    void RestoreEventCallback();
    paf::ui::ListItem *CreateListItem(paf::ui::listview::ItemFactory::CreateParam &param);

private:
    struct AppButtonContext {
        Apps *page;
        int index;
    };

    static void OnAppButton(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void OnMoonlightEvent(const MoonlightEvent *event, void *userdata);
    void SetStatus(const char *text);
    void ShowApps();
    void SelectApp(int index);

    MoonlightApplication m_apps[8];
    int m_app_count;
    int m_selected_index;
    bool m_launching;
    AppButtonContext m_button_contexts[8];
    paf::ui::ListView *m_list;

};

}

#endif
