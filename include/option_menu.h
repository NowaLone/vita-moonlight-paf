#ifndef VITA_MOONLIGHT_OPTION_MENU_H
#define VITA_MOONLIGHT_OPTION_MENU_H

#include <paf.h>

class OptionMenu {
public:
    enum EventType {
        Event_Close = 0,
        Event_Button = 1
    };

    typedef void (*EventCb)(EventType type, int button_index, void *userdata);

    OptionMenu(paf::Plugin *plugin, paf::ui::Widget *parent, EventCb cb, void *userdata);
    ~OptionMenu();

    static OptionMenu *Instance();

private:
    static void OnDismiss(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);
    static void OnSettings(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);

    paf::Plugin *m_plugin;
    paf::ui::Scene *m_scene;
    paf::ui::Widget *m_parent;
    EventCb m_cb;
    void *m_userdata;
};

#endif
