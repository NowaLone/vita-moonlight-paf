#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include "pages/page.h"
#include "common.h"

namespace page {

static paf::vector<Base *> *s_stack = NULL;

static paf::vector<Base *> &stack()
{
    if (s_stack == NULL) {
        s_stack = new paf::vector<Base *>();
    }
    return *s_stack;
}

static int id_eq(const char *a, const char *b)
{
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;
    return sce_paf_strcmp(a, b) == 0;
}

Base::Base(const char *id, const char *back_id,
           paf::Plugin::TransitionType open_transition,
           paf::Plugin::TransitionType close_transition)
    : root(NULL),
      m_id(id),
      m_back(NULL),
      m_registered(false)
{
    m_close_param = make_close_param(close_transition);

    if (!g_plugin || !id || IsOpen(id)) {
        return;
    }

    paf::Plugin::PageOpenParam open_param = make_open_param(open_transition);

    /*
     * Full-screen playback pages in PAF applications use the same graphics
     * mode as NetStream. Without graphics_flag/draw-priority the page can
     * exist in the page stack without participating in the intended render
     * layer.
     */
    if (sce_paf_strcmp(id, "page_stream") == 0 ||
        sce_paf_strcmp(id, "page_settings_bubble") == 0) {
        open_param.graphics_flag = 0x80;
        open_param.overwrite_draw_priority = 8;
        open_param.fade = false;
    }

    if (sce_paf_strcmp(id, "page_settings_bubble") == 0) {
        open_param.option = paf::Plugin::PageOption_Create;
    }

    root = g_plugin->PageOpen(id, open_param);
    if (root == NULL) {
        return;
    }

    Base *previous = GetCurrent();
    if (previous && previous->root) {
        previous->root->SetActivate(false);
    }

    if (back_id) {
        m_back = root->FindChild(back_id);
        if (m_back) {
            if (previous) {
                m_back->Show(paf::common::transition::Type_Reset);
            } else {
                m_back->Hide(paf::common::transition::Type_Reset);
            }

            m_back->SetEventCallback(
                paf::ui::ButtonBase::CB_BTN_DECIDE,
                DefaultBackButtonCB,
                this
            );
        }
    }

    stack().push_back(this);
    m_registered = true;
}

Base::~Base()
{
    if (!m_registered) {
        return;
    }

    for (paf::vector<Base *>::iterator it = stack().begin(); it != stack().end(); ++it) {
        if (*it == this) {
            stack().erase(it);
            break;
        }
    }

    if (g_plugin && m_id && root) {
        g_plugin->PageClose(m_id, m_close_param);
    }

    Base *current = GetCurrent();
    if (current && current->root) {
        current->root->SetActivate(true);
    }

    m_registered = false;
}

Base *Base::GetCurrent()
{
    return stack().empty() ? NULL : stack().back();
}

bool Base::IsOpen(const char *id)
{
    return Find(id) != NULL;
}

Base *Base::Find(const char *id)
{
    for (paf::vector<Base *>::iterator it = stack().begin(); it != stack().end(); ++it) {
        Base *page = *it;
        if (page && id_eq(page->m_id, id)) {
            return page;
        }
    }
    return NULL;
}

void Base::CloseCurrent()
{
    Base *current = GetCurrent();
    if (current) {
        delete current;
    }
}

void Base::DefaultBackButtonCB(int32_t type,
                               paf::ui::Handler *self,
                               paf::ui::Event *event,
                               void *userdata)
{
    (void)type;
    (void)self;
    (void)event;
    (void)userdata;
    CloseCurrent();
}

void Base::CloseType(Type type)
{
    for (int i = (int)stack().size() - 1; i >= 0; --i) {
        if (stack()[i] && stack()[i]->GetType() == type) {
            delete stack()[i];
            return;
        }
    }
}

Base *Find(const char *id)
{
    return Base::Find(id);
}

} // namespace page
