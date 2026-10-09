#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include "common.h"
#include <paf/widget/w_button.h>

paf::Plugin *g_plugin = NULL;

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

    /* Set interaction state without fading the button's background texture. */
    button->SetActivate(enabled);
    paf::graph::DrawObj *label = button->GetDrawObj(paf::ui::Button::OBJ_LABEL);
    if (label != NULL) {
        const float text_color = enabled ? 1.0f : 0.55f;
        label->SetColor(paf::math::v4(
            text_color,
            text_color,
            text_color,
            1.0f
        ));
    }
}
