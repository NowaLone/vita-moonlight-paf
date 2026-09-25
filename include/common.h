#ifndef VITA_MOONLIGHT_COMMON_H
#define VITA_MOONLIGHT_COMMON_H

#include <paf.h>

extern paf::Plugin *g_plugin;

typedef void (*DecideCb)(int32_t type, paf::ui::Handler *self, paf::ui::Event *e, void *userdata);

paf::Plugin::PageOpenParam make_open_param(paf::Plugin::TransitionType transition);
paf::Plugin::PageCloseParam make_close_param(paf::Plugin::TransitionType transition);

void bind_decide(paf::ui::Widget *root, const char *child_id, DecideCb cb, void *userdata = NULL);
void set_widget_focusable(paf::ui::Widget *widget, bool on);

#endif
