#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include "pages/page.h"
#include "common.h"

namespace page {

static const int kMaxPages = 8;
static Base *s_stack[kMaxPages];
static int s_depth = 0;

static int id_eq(const char *a, const char *b) {
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    return sce_paf_strcmp(a, b) == 0;
}

Base::Base(const char *id,
           const char *back_id,
           paf::Plugin::TransitionType open_transition,
           paf::Plugin::TransitionType close_transition)
    : root(NULL), m_id(id), m_back(NULL) {
    m_close_param = make_close_param(close_transition);
    if (g_plugin == NULL || id == NULL || s_depth >= kMaxPages) {
        return;
    }
    if (IsOpen(id)) {
        root = Find(id) != NULL ? Find(id)->root : NULL;
        return;
    }

    root = g_plugin->PageOpen(id, make_open_param(open_transition));
    if (root == NULL) {
        return;
    }

    if (s_depth > 0 && s_stack[s_depth - 1] != NULL && s_stack[s_depth - 1]->root != NULL) {
        s_stack[s_depth - 1]->root->SetActivate(false);
    }

    if (back_id != NULL) {
        m_back = root->FindChild(back_id);
        if (m_back != NULL && s_depth > 0) {
            m_back->Show(paf::common::transition::Type_Reset);
        } else if (m_back != NULL) {
            m_back->Hide(paf::common::transition::Type_Reset);
        }
    }

    s_stack[s_depth++] = this;
}

Base::~Base() {
    int found = -1;
    for (int i = 0; i < s_depth; i++) {
        if (s_stack[i] == this) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        for (int i = found; i < s_depth - 1; i++) {
            s_stack[i] = s_stack[i + 1];
        }
        s_depth--;
    }

    if (g_plugin != NULL && m_id != NULL && root != NULL) {
        g_plugin->PageClose(m_id, m_close_param);
    }

    if (s_depth > 0 && s_stack[s_depth - 1] != NULL && s_stack[s_depth - 1]->root != NULL) {
        s_stack[s_depth - 1]->root->SetActivate(true);
    }
}

Base *Base::GetCurrent() {
    if (s_depth <= 0) {
        return NULL;
    }
    return s_stack[s_depth - 1];
}

Base *Base::GetAt(int index) {
    if (index < 0 || index >= s_depth) {
        return NULL;
    }
    return s_stack[index];
}

int Base::Count() {
    return s_depth;
}

bool Base::IsOpen(const char *id) {
    return Find(id) != NULL;
}

Base *Base::Find(const char *id) {
    for (int i = 0; i < s_depth; i++) {
        if (s_stack[i] != NULL && id_eq(s_stack[i]->m_id, id)) {
            return s_stack[i];
        }
    }
    return NULL;
}

void Base::DeleteCurrent() {
    if (s_depth <= 0) {
        return;
    }
    delete s_stack[s_depth - 1];
}

void Base::CloseType(Type type) {
    for (int i = s_depth - 1; i >= 0; i--) {
        if (s_stack[i] != NULL && s_stack[i]->GetType() == type) {
            delete s_stack[i];
            return;
        }
    }
}

void Base::CloseUntil(Type keep_type) {
    while (s_depth > 0) {
        Base *top = s_stack[s_depth - 1];
        if (top == NULL || top->GetType() == keep_type) {
            break;
        }
        delete top;
    }
}

void SetMainButtonsFocusable(bool on) {
    Base *main = Find("page_main");
    if (main == NULL || main->root == NULL) {
        return;
    }
    const char *ids[] = { "btn_search_pcs", "btn_add_manually", "settings_button" };
    for (int i = 0; i < 3; i++) {
        set_widget_focusable(main->root->FindChild(ids[i]), on);
    }
}

}
