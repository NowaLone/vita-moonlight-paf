#include "option_menu.h"
#include "common.h"
static OptionMenu*s_instance=NULL;
OptionMenu*OptionMenu::Instance(){return s_instance;}
void OptionMenu::OnDismiss(int32_t t,paf::ui::Handler*s,paf::ui::Event*e,void*u){(void)t;(void)s;(void)e;OptionMenu*m=(OptionMenu*)u;if(!m)return;EventCb cb=m->m_cb;void*d=m->m_userdata;delete m;if(cb)cb(Event_Close,-1,d);}
void OptionMenu::OnSettings(int32_t t,paf::ui::Handler*s,paf::ui::Event*e,void*u){(void)t;(void)s;(void)e;OptionMenu*m=(OptionMenu*)u;if(!m)return;if(m->root)m->root->Hide(paf::common::transition::Type_Reset);EventCb cb=m->m_cb;void*d=m->m_userdata;delete m;if(cb)cb(Event_Button,0,d);}
OptionMenu::OptionMenu(paf::Plugin*plugin,paf::ui::Widget*parent,EventCb cb,void*userdata)
:page::Base("page_settings_bubble",NULL,paf::Plugin::TransitionType_None,paf::Plugin::TransitionType_None),m_cb(cb),m_userdata(userdata){
 (void)plugin;(void)parent;if(!IsValid())return;s_instance=this;
 paf::ui::Widget*b=root->FindChild("settings_speech_balloon");if(b){const float w=202.0f,pw=w+12.0f,ph=72.0f;b->SetSize({pw,ph,0,0},NULL);b->SetPos(264.0f+((w-pw)/2.0f),43.0f,0,NULL);b->Show(paf::common::transition::Type_Popup4,0.0f);}
 bind_decide(root,"btn_settings_balloon",OnSettings,this);bind_decide(root,"btn_dismiss_balloon",OnDismiss,this);set_widget_focusable(root->FindChild("btn_dismiss_balloon"),false);
 paf::ui::Widget*sb=root->FindChild("btn_settings_balloon");if(sb)sb->SetFocusedState(true);
}
OptionMenu::~OptionMenu(){if(s_instance==this)s_instance=NULL;}
