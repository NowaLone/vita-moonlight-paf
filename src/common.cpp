#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <stdarg.h>
#include <stdio.h>

#include "common.h"
#include "debug.h"
#include <paf/widget/w_button_base.h>

paf::Plugin *g_plugin = NULL;
static int s_debug_log_enabled = 0;

extern "C" void vita_debug_log_set_enabled(int enabled)
{
    s_debug_log_enabled = enabled ? 1 : 0;
}

extern "C" void vita_debug_log(const char *fmt, ...)
{
    char line[512];
    va_list args;
    int length;

    if (!s_debug_log_enabled || !fmt) {
        return;
    }

    va_start(args, fmt);
    length = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (length < 0) {
        return;
    }
    if (length >= (int)sizeof(line)) {
        length = (int)sizeof(line) - 1;
    }

    sceClibPrintf("%s\n", line);

}

paf::Plugin::PageOpenParam make_open_param(paf::Plugin::TransitionType transition) {
    paf::Plugin::PageOpenParam param;
    param.option = paf::Plugin::PageOption_None;
    param.fade = true;
    param.fade_time_ms = 200.0f;
    param.transition_type = transition;
    return param;
}

paf::Plugin::PageCloseParam make_close_param(paf::Plugin::TransitionType transition) {
    paf::Plugin::PageCloseParam param;
    param.fade = true;
    param.fade_time_ms = 200.0f;
    param.transition_type = transition;
    return param;
}

void bind_decide(paf::ui::Widget *root, const char *child_id, DecideCb cb, void *userdata) {
    if (root == NULL || child_id == NULL || cb == NULL) {
        return;
    }
    paf::ui::Widget *child = root->FindChild(child_id);
    if (child != NULL) {
        child->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, cb, userdata);
    }
}

void set_widget_focusable(paf::ui::Widget *widget, bool on) {
    if (widget == NULL) {
        return;
    }
    if (on) {
        widget->EnableEvent(paf::ui::EV_FOCUS);
    } else {
        widget->EnableFocusEvent(false);
        widget->DisableEvent(paf::ui::EV_FOCUS);
        widget->ReleaseFocus();
    }
}

void set_button_enabled(paf::ui::Widget *button, bool enabled) {
    if (button == NULL) {
        return;
    }

    /*
     * Use PAF's native ButtonBase disabled state and its disabled text color;
     * SetActivate is kept in sync because it controls widget interaction.
     */
    paf::ui::ButtonBase *button_base =
        static_cast<paf::ui::ButtonBase *>(button);
    button_base->SetDisableColor(0.55f, 0.55f, 0.55f, 1.0f);
    if (enabled) {
        button_base->Enable();
    } else {
        button_base->Disable();
    }
    button->SetActivate(enabled);
}
