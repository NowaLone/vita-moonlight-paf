#ifndef VITA_MOONLIGHT_OPTION_MENU_H
#define VITA_MOONLIGHT_OPTION_MENU_H
#include "pages/page.h"
class OptionMenu : public page::Base {
public:
 enum EventType { Event_Close=0, Event_Button=1 }; typedef void(*EventCb)(EventType,int,void*);
 OptionMenu(paf::Plugin*plugin,paf::ui::Widget*parent,EventCb cb,void*userdata);
 virtual ~OptionMenu(); virtual page::Type GetType(){return page::Type_OptionMenu;}
 static OptionMenu*Instance();
private:
 static void OnDismiss(int32_t,paf::ui::Handler*,paf::ui::Event*,void*);
 static void OnSettings(int32_t,paf::ui::Handler*,paf::ui::Event*,void*);
 EventCb m_cb; void*m_userdata;
};
#endif
