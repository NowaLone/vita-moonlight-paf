#ifndef VITA_MOONLIGHT_PAGE_H
#define VITA_MOONLIGHT_PAGE_H
#include <paf.h>
namespace page {
enum Type { Type_Main, Type_Search, Type_AddHost, Type_Apps, Type_Stream, Type_OptionMenu };
class Base {
public:
    Base(const char *id,const char *back_id,paf::Plugin::TransitionType open_transition,paf::Plugin::TransitionType close_transition);
    virtual ~Base();
    virtual Type GetType() = 0;
    const char *Id() const { return m_id; }
    paf::ui::Scene *Root() const { return root; }
    bool IsValid() const { return m_registered; }
    static Base *GetCurrent();
    static bool IsOpen(const char *id);
    static Base *Find(const char *id);
    static void CloseCurrent();
    static void DefaultBackButtonCB(int32_t type, paf::ui::Handler *self, paf::ui::Event *event, void *userdata);
    static void CloseType(Type type);
    paf::ui::Scene *root;
protected:
    const char *m_id; paf::ui::Widget *m_back; paf::Plugin::PageCloseParam m_close_param; bool m_registered;
};
Base *Find(const char *id);
}
#endif
