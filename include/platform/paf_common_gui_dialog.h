#ifndef VITA_MOONLIGHT_PAF_COMMON_GUI_DIALOG_H
#define VITA_MOONLIGHT_PAF_COMMON_GUI_DIALOG_H

#include <stdint.h>
#include <paf.h>

/*
 * VitaSDK-compatible declarations for the native busy-dialog API we use.
 * Matches vitasdk-paf-component's common_gui_dialog.h and SceCommonGuiDialog_stub.
 * The upstream umbrella header includes Sony-SDK kernel.h/appmgr.h and unrelated
 * ErrorDialog definitions. Keep this binding limited to the real Show/Close ABI.
 * Do not include both headers in one translation unit.
 */
namespace sce {
namespace CommonGuiDialog {

class BaseHashTable;
class ContentsHashTable;

enum DIALOG_CB {
    DIALOG_CB_X = 1,
    DIALOG_CB_OK,
    DIALOG_CB_CANCEL,
    DIALOG_CB_YES,
    DIALOG_CB_NO,
    DIALOG_CB_UNK6,
    DIALOG_CB_JOINPSPLUS,
    DIALOG_CB_ABOUTPSPLUS
};

class Param {
public:
    BaseHashTable *base_list;
    int32_t unk_04;
    ContentsHashTable *contents_list;

    static Param s_dialogTextSmallBusy;
};

class Dialog {
public:
    static int32_t Show(paf::Plugin *work_plugin, paf::wstring *title,
                        paf::wstring *message, Param *param,
                        void (*button_cb)(int32_t, DIALOG_CB, void *), void *userdata);
    /* Keep upstream's declaration, but discard the return: the inspected native
     * implementation does not produce a status code. See docs/UI.md. */
    static int32_t Close(int32_t instance_slot);
};

} // namespace CommonGuiDialog
} // namespace sce

#endif
