#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif
#include "pages/page.h"
#include "common.h"
namespace page {
static paf::vector<Base *> *s_stack = NULL;
static paf::vector<Base *> &stack() {
 if (s_stack == NULL) s_stack = new paf::vector<Base *>();
 return *s_stack;
}
static int id_eq(const char*a,const char*b){if(a==b)return 1;if(!a||!b)return 0;return sce_paf_strcmp(a,b)==0;}
Base::Base(const char*id,const char*back_id,paf::Plugin::TransitionType open_transition,paf::Plugin::TransitionType close_transition)
:root(NULL),m_id(id),m_back(NULL),m_registered(false){
 m_close_param=make_close_param(close_transition);
 if(!g_plugin||!id||IsOpen(id))return;
 root=g_plugin->PageOpen(id,make_open_param(open_transition)); if(!root)return;
 Base*previous=GetCurrent(); if(previous&&previous->root)previous->root->SetActivate(false);
 if(back_id){m_back=root->FindChild(back_id);if(m_back){if(previous)m_back->Show(paf::common::transition::Type_Reset);else m_back->Hide(paf::common::transition::Type_Reset);}}
 stack().push_back(this);m_registered=true;
}
Base::~Base(){if(!m_registered)return;for(paf::vector<Base*>::iterator it=stack().begin();it!=stack().end();++it){if(*it==this){stack().erase(it);break;}}if(g_plugin&&m_id&&root)g_plugin->PageClose(m_id,m_close_param);Base*cur=GetCurrent();if(cur&&cur->root)cur->root->SetActivate(true);m_registered=false;}
Base*Base::GetCurrent(){return stack().empty()?NULL:stack().back();}
Base*Base::GetAt(int i){return i>=0&&i<(int)stack().size()?stack()[i]:NULL;}
int Base::Count(){return (int)stack().size();}
bool Base::IsOpen(const char*id){return Find(id)!=NULL;}
Base*Base::Find(const char*id){for(paf::vector<Base*>::iterator it=stack().begin();it!=stack().end();++it){Base*p=*it;if(p&&id_eq(p->m_id,id))return p;}return NULL;}
void Base::DeleteCurrent(){Base*cur=GetCurrent();if(cur)delete cur;}
void Base::CloseType(Type type){for(int i=(int)stack().size()-1;i>=0;--i){if(stack()[i]&&stack()[i]->GetType()==type){delete stack()[i];return;}}}
void Base::CloseUntil(Type keep_type){while(!stack().empty()){Base*top=GetCurrent();if(!top||top->GetType()==keep_type)break;delete top;}}
Base*Find(const char*id){return Base::Find(id);}
void SetMainButtonsFocusable(bool on){Base*main=Find("page_main");if(!main||!main->root)return;const char*ids[]={"btn_search_pcs","btn_add_manually","settings_button"};for(int i=0;i<3;++i)set_widget_focusable(main->root->FindChild(ids[i]),on);}
}
