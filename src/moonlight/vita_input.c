#include "vita_input.h"

#include <Limelight.h>
#include <psp2/ctrl.h>
#include <psp2/shellutil.h>
#include <psp2/kernel/threadmgr.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "debug.h"
#include "moonlight/stream_session.h"
#include "vita_touch.h"
#include "vita_motion.h"

static volatile bool s_input_active = false;
static bool s_thread_started = false;
static uint8_t s_controller_type = LI_CTYPE_PS;
static bool s_swap_shoulder_buttons = false;
static bool s_ps_button_capture = true;
static bool s_ps_button_locked = false;
static int s_touchscreen_mode = 0;
static int s_mouse_acceleration = 0;
static bool s_motion_enabled = false;

static SceCtrlData s_pad;
static SceCtrlData s_pad_old;

static short vita_axis_to_limelight(unsigned char value)
{
    /*
     * Keep the same conversion used by the original Vita Moonlight input
     * implementation.
     */
    int v = (int)value * 256 - (1 << 15) + 128;
    return (short)v;
}

static uint8_t get_controller_type(int controller_type)
{
    switch (controller_type) {
        case 1:
            return LI_CTYPE_XBOX;
        case 2:
            return LI_CTYPE_PS;
        case 3:
            return LI_CTYPE_NINTENDO;
        case 4:
            return LI_CTYPE_UNKNOWN;
        default:
            return LI_CTYPE_PS;
    }
}

static void vita_input_send(void)
{
    int button_flags = 0;
    unsigned char left_trigger = 0;
    unsigned char right_trigger = 0;

    if (s_pad.buttons & SCE_CTRL_UP) {
        button_flags |= UP_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_DOWN) {
        button_flags |= DOWN_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_LEFT) {
        button_flags |= LEFT_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_RIGHT) {
        button_flags |= RIGHT_FLAG;
    }

    if (s_pad.buttons & SCE_CTRL_START) {
        button_flags |= PLAY_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_SELECT) {
        button_flags |= BACK_FLAG;
    }

    if (s_pad.buttons & SCE_CTRL_TRIANGLE) {
        button_flags |= Y_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_CIRCLE) {
        button_flags |= B_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_CROSS) {
        button_flags |= A_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_SQUARE) {
        button_flags |= X_FLAG;
    }

    if (s_swap_shoulder_buttons) {
        if (s_pad.buttons & SCE_CTRL_L1) {
            left_trigger = 0xFF;
        }
        if (s_pad.buttons & SCE_CTRL_R1) {
            right_trigger = 0xFF;
        }
    } else {
        if (s_pad.buttons & SCE_CTRL_L1) {
            button_flags |= LB_FLAG;
        }
        if (s_pad.buttons & SCE_CTRL_R1) {
            button_flags |= RB_FLAG;
        }
    }

    if (s_pad.buttons & SCE_CTRL_L3) {
        button_flags |= LS_CLK_FLAG;
    }
    if (s_pad.buttons & SCE_CTRL_R3) {
        button_flags |= RS_CLK_FLAG;
    }

    LiSendMultiControllerEvent(
        0,
        1,
        button_flags,
        left_trigger,
        right_trigger,
        vita_axis_to_limelight(s_pad.lx),
        -vita_axis_to_limelight(s_pad.ly),
        vita_axis_to_limelight(s_pad.rx),
        -vita_axis_to_limelight(s_pad.ry));
}

static void vita_input_process(void)
{
    bool ps_pressed;
    bool ps_pressed_old;

    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlPeekBufferPositiveExt2(0, &s_pad, 1);

    ps_pressed = (s_pad.buttons & SCE_CTRL_PSBUTTON) != 0;
    ps_pressed_old = (s_pad_old.buttons & SCE_CTRL_PSBUTTON) != 0;

    if (s_ps_button_capture && ps_pressed && !ps_pressed_old) {
        vita_debug_log("[Input] PS Button pressed, stopping stream");
        moonlight_stream_stop();
        s_pad.buttons &= ~SCE_CTRL_PSBUTTON;
    }

    if (memcmp(&s_pad, &s_pad_old, sizeof(SceCtrlData)) != 0) {
        vita_input_send();
        memcpy(&s_pad_old, &s_pad, sizeof(SceCtrlData));
    }

    vita_touch_process();
}

static int vita_input_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    while (1) {
        if (s_input_active) {
            vita_input_process();
        }

        sceKernelDelayThread(2000);
    }

    return 0;
}

static void vita_input_ensure_thread(void)
{
    if (s_thread_started) {
        return;
    }

    SceUID thread_id = sceKernelCreateThread(
        "vita_moonlight_input",
        vita_input_thread,
        0x40,
        0x40000,
        0,
        0,
        NULL);

    if (thread_id < 0) {
        vita_debug_log(
            "[Input] create thread failed result=0x%08x",
            (unsigned int)thread_id);
        return;
    }

    int result = sceKernelStartThread(thread_id, 0, NULL);
    if (result < 0) {
        vita_debug_log(
            "[Input] start thread failed result=0x%08x",
            (unsigned int)result);
        sceKernelDeleteThread(thread_id);
        return;
    }

    s_thread_started = true;
}

int vita_input_configure(
    int controller_type,
    int swap_shoulder_buttons,
    int ps_button_capture)
{
    s_controller_type = get_controller_type(controller_type);
    s_swap_shoulder_buttons = swap_shoulder_buttons != 0;
    s_ps_button_capture = ps_button_capture != 0;
    s_touchscreen_mode = touchscreen_mode;
    s_mouse_acceleration = mouse_acceleration;
    s_motion_enabled = motion_enabled != 0;

    vita_touch_configure(
        s_touchscreen_mode,
        s_mouse_acceleration);

    return 0;
}

void vita_input_start(void)
{
    vita_input_ensure_thread();

    memset(&s_pad, 0, sizeof(s_pad));
    memset(&s_pad_old, 0, sizeof(s_pad_old));

    /*
     * Match the controller declaration used by the original Vita Moonlight.
     * We currently implement the standard Vita buttons and both analog sticks.
     */
    uint32_t supported_buttons = 0xFFFF;
    uint16_t capabilities = LI_CCAP_ANALOG_TRIGGERS;

    if (s_touchscreen_mode == 1) {
        supported_buttons |= TOUCHPAD_FLAG;
        capabilities |= LI_CCAP_TOUCHPAD;
    }

    if (s_motion_enabled) {
        capabilities |= LI_CCAP_GYRO | LI_CCAP_ACCEL;
    }

    LiSendControllerArrivalEvent(
        0,
        1,
        s_controller_type,
        supported_buttons,
        capabilities);

    if (s_ps_button_capture && !s_ps_button_locked) {
        int result = sceShellUtilLock(
            (SceShellUtilLockType)(
                SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN |
                SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2));

        if (result == 0) {
            s_ps_button_locked = true;
        }

        vita_debug_log(
            "[Input] PS Button lock result=0x%08x",
            (unsigned int)result);
    }

    vita_touch_start();
    vita_motion_start(s_motion_enabled ? 1 : 0);
    s_input_active = true;

    vita_debug_log(
        "[Input] start type=%u swap_shoulders=%d ps_capture=%d touch_mode=%d motion=%d",
        (unsigned int)s_controller_type,
        s_swap_shoulder_buttons ? 1 : 0,
        s_ps_button_capture ? 1 : 0,
        s_touchscreen_mode,
        s_motion_enabled ? 1 : 0);
}

void vita_input_stop(void)
{
    s_input_active = false;

    vita_touch_stop();
    vita_motion_stop();

    /*
     * Explicitly release every host-side button/trigger before the input
     * stream is torn down. This also guarantees that Circle cannot remain
     * stuck when the stream is stopped.
     */
    LiSendMultiControllerEvent(
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0);

    memset(&s_pad, 0, sizeof(s_pad));
    memset(&s_pad_old, 0, sizeof(s_pad_old));

    if (s_ps_button_locked) {
        int result = sceShellUtilUnlock(
            (SceShellUtilLockType)(
                SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN |
                SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2));

        vita_debug_log(
            "[Input] PS Button unlock result=0x%08x",
            (unsigned int)result);

        s_ps_button_locked = false;
    }

    vita_debug_log("[Input] stop");
}

void vita_input_set_motion_state(
    uint16_t controller,
    uint8_t motion_type,
    uint16_t report_rate_hz)
{
    vita_motion_set_state(
        controller,
        motion_type,
        report_rate_hz);
}
