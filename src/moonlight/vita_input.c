#include "vita_input.h"

#include <Limelight.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/shellutil.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>

#ifndef SCE_POWER_TICK_DISABLE_AUTO_SUSPEND
#define SCE_POWER_TICK_DISABLE_AUTO_SUSPEND 1
#endif

extern int scePowerTick(int type);
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
static int s_back_deadzone_top = 0;
static int s_back_deadzone_right = 0;
static int s_back_deadzone_bottom = 0;
static int s_back_deadzone_left = 0;
static int s_swap_xo = 0;
static int s_front_touchzones = 0;
static int s_double_tap_sprint = 0;
static int s_double_tap_ms = 200;
static int s_keyboard_layout = 0;
static int s_mapping_enabled = 0;
static int s_disable_powersave = 1;
static int s_keyboard_request = 0;
static int s_zone_latched = 0;
static uint64_t s_last_sprint_tap = 0;
static int s_sprint_was_forward = 0;
static int s_face_cross = A_FLAG;
static int s_face_circle = B_FLAG;
static int s_face_square = X_FLAG;
static int s_face_triangle = Y_FLAG;

static SceCtrlData s_pad;
static SceCtrlData s_pad_old;
static SceTouchData s_back;
static uint32_t s_back_buttons_old = 0;

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

static uint32_t vita_read_back_touch(const SceTouchData *back)
{
    enum {
        BACK_NORTHWEST = 0x01,
        BACK_NORTHEAST = 0x02,
        BACK_SOUTHWEST = 0x04,
        BACK_SOUTHEAST = 0x08
    };

    const int vertical =
        (960 - s_back_deadzone_left - s_back_deadzone_right) / 2 +
        s_back_deadzone_left;
    const int horizontal =
        (544 - s_back_deadzone_top - s_back_deadzone_bottom) / 2 +
        s_back_deadzone_top;

    uint32_t buttons = 0;
    unsigned int i;

    for (i = 0; i < back->reportNum && i < SCE_TOUCH_MAX_REPORT; ++i) {
        int x = (int)back->report[i].x * 960 / 1920;
        int y = (int)back->report[i].y * 544 / 1088;

        if (x < s_back_deadzone_left ||
            x > 960 - s_back_deadzone_right ||
            y < s_back_deadzone_top ||
            y > 544 - s_back_deadzone_bottom) {
            continue;
        }

        if (x <= vertical && y <= horizontal) {
            buttons |= BACK_NORTHWEST;
        } else if (x > vertical && y <= horizontal) {
            buttons |= BACK_NORTHEAST;
        } else if (x <= vertical && y > horizontal) {
            buttons |= BACK_SOUTHWEST;
        } else {
            buttons |= BACK_SOUTHEAST;
        }
    }

    return buttons;
}


static void vita_input_send(uint32_t back_buttons)
{
    const uint32_t BACK_NORTHWEST = 0x01;
    const uint32_t BACK_NORTHEAST = 0x02;
    const uint32_t BACK_SOUTHWEST = 0x04;
    const uint32_t BACK_SOUTHEAST = 0x08;
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
        button_flags |= s_face_triangle;
    }
    if (s_pad.buttons & SCE_CTRL_CIRCLE) {
        button_flags |= s_face_circle;
    }
    if (s_pad.buttons & SCE_CTRL_CROSS) {
        button_flags |= s_face_cross;
    }
    if (s_pad.buttons & SCE_CTRL_SQUARE) {
        button_flags |= s_face_square;
    }

    if (s_swap_shoulder_buttons) {
        if (s_pad.buttons & SCE_CTRL_L1) {
            left_trigger = 0xFF;
        }
        if (s_pad.buttons & SCE_CTRL_R1) {
            right_trigger = 0xFF;
        }
        if (back_buttons & BACK_NORTHWEST) {
            button_flags |= LB_FLAG;
        }
        if (back_buttons & BACK_NORTHEAST) {
            button_flags |= RB_FLAG;
        }
    } else {
        if (s_pad.buttons & SCE_CTRL_L1) {
            button_flags |= LB_FLAG;
        }
        if (s_pad.buttons & SCE_CTRL_R1) {
            button_flags |= RB_FLAG;
        }
        if (back_buttons & BACK_NORTHWEST) {
            left_trigger = 0xFF;
        }
        if (back_buttons & BACK_NORTHEAST) {
            right_trigger = 0xFF;
        }
    }

    if (back_buttons & BACK_SOUTHWEST) {
        button_flags |= LS_CLK_FLAG;
    }
    if (back_buttons & BACK_SOUTHEAST) {
        button_flags |= RS_CLK_FLAG;
    }

    if (s_double_tap_sprint) {
        int forward = s_pad.ly > 200;
        uint64_t now = sceKernelGetSystemTimeWide();
        if (forward && !s_sprint_was_forward) {
            if (s_last_sprint_tap != 0 &&
                now - s_last_sprint_tap <=
                    (uint64_t)s_double_tap_ms * 1000ull) {
                button_flags |= LS_CLK_FLAG;
                s_last_sprint_tap = 0;
            } else {
                s_last_sprint_tap = now;
            }
        }
        s_sprint_was_forward = forward;
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
    uint32_t back_buttons;

    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlPeekBufferPositiveExt2(0, &s_pad, 1);
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &s_back, 1);

    if (s_disable_powersave) {
        scePowerTick(SCE_POWER_TICK_DISABLE_AUTO_SUSPEND);
    }

    if (s_front_touchzones && s_touchscreen_mode == 0) {
        SceTouchData front;
        memset(&front, 0, sizeof(front));
        if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) >= 0 &&
            front.reportNum > 0) {
            int x;
            int y;
            if (s_zone_latched) {
                goto front_done;
            }
            s_zone_latched = 1;
            x = (int)front.report[0].x * 960 / 1920;
            y = (int)front.report[0].y * 544 / 1088;
            if (x < 140 && y < 100) {
                s_keyboard_request = 1;
            } else if (x > 820 && y < 100) {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
            } else if (x < 140 && y > 444) {
                LiSendMultiControllerEvent(
                    0, 1, LS_CLK_FLAG, 0, 0, 0, 0, 0, 0);
            } else if (x > 820 && y > 444) {
                LiSendMultiControllerEvent(
                    0, 1, RS_CLK_FLAG, 0, 0, 0, 0, 0, 0);
            }
        } else {
            s_zone_latched = 0;
        }
front_done:
        ;
    }

    ps_pressed = (s_pad.buttons & SCE_CTRL_PSBUTTON) != 0;
    ps_pressed_old = (s_pad_old.buttons & SCE_CTRL_PSBUTTON) != 0;

    if (s_ps_button_capture && ps_pressed && !ps_pressed_old) {
        vita_debug_log("[Input] PS Button pressed, stopping stream");
        moonlight_stream_stop();
        s_pad.buttons &= ~SCE_CTRL_PSBUTTON;
    }

    back_buttons = vita_read_back_touch(&s_back);

    if (memcmp(&s_pad, &s_pad_old, sizeof(SceCtrlData)) != 0 ||
        back_buttons != s_back_buttons_old) {
        vita_input_send(back_buttons);
        memcpy(&s_pad_old, &s_pad, sizeof(SceCtrlData));
        s_back_buttons_old = back_buttons;
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
    int ps_button_capture,
    int touchscreen_mode,
    int mouse_acceleration,
    int motion_enabled,
    int back_deadzone_top,
    int back_deadzone_right,
    int back_deadzone_bottom,
    int back_deadzone_left)
{
    s_controller_type = get_controller_type(controller_type);
    s_swap_shoulder_buttons = swap_shoulder_buttons != 0;
    s_ps_button_capture = ps_button_capture != 0;
    s_touchscreen_mode = touchscreen_mode;
    s_mouse_acceleration = mouse_acceleration;
    s_motion_enabled = motion_enabled != 0;
    s_back_deadzone_top = back_deadzone_top;
    s_back_deadzone_right = back_deadzone_right;
    s_back_deadzone_bottom = back_deadzone_bottom;
    s_back_deadzone_left = back_deadzone_left;

    if (s_back_deadzone_top < 0) s_back_deadzone_top = 0;
    if (s_back_deadzone_right < 0) s_back_deadzone_right = 0;
    if (s_back_deadzone_bottom < 0) s_back_deadzone_bottom = 0;
    if (s_back_deadzone_left < 0) s_back_deadzone_left = 0;
    if (s_back_deadzone_top > 480) s_back_deadzone_top = 480;
    if (s_back_deadzone_right > 480) s_back_deadzone_right = 480;
    if (s_back_deadzone_bottom > 480) s_back_deadzone_bottom = 480;
    if (s_back_deadzone_left > 480) s_back_deadzone_left = 480;

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
    memset(&s_back, 0, sizeof(s_back));
    s_back_buttons_old = 0;

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

static void vita_input_load_mapping(void)
{
    SceUID fd;
    char buffer[1024];
    int length;
    char *line;

    s_face_cross = A_FLAG;
    s_face_circle = B_FLAG;
    s_face_square = X_FLAG;
    s_face_triangle = Y_FLAG;
    if (!s_mapping_enabled) {
        return;
    }
    if (s_swap_xo) {
        s_face_cross = B_FLAG;
        s_face_circle = A_FLAG;
    }

    fd = sceIoOpen("ux0:data/moonlight/vita.conf", SCE_O_RDONLY, 0);
    if (fd < 0) {
        sceIoMkdir("ux0:data/moonlight", 0777);
        fd = sceIoOpen(
            "ux0:data/moonlight/vita.conf",
            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
            0666);
        if (fd >= 0) {
            const char *defaults =
                "# physical=moonlight face button (a b x y)\n"
                "cross=a\n"
                "circle=b\n"
                "square=x\n"
                "triangle=y\n";
            sceIoWrite(fd, defaults, strlen(defaults));
            sceIoClose(fd);
        }
        vita_debug_log("[Input] wrote default mapping ux0:data/moonlight/vita.conf");
        return;
    }

    length = sceIoRead(fd, buffer, sizeof(buffer) - 1);
    sceIoClose(fd);
    if (length <= 0) {
        return;
    }
    buffer[length] = '\0';
    line = buffer;
    while (line && *line) {
        char *next = strchr(line, '\n');
        char *eq;
        int flag = 0;
        if (next) {
            *next = '\0';
            ++next;
        }
        eq = strchr(line, '=');
        if (eq && line[0] != '#') {
            *eq = '\0';
            if (strcmp(eq + 1, "a") == 0) flag = A_FLAG;
            else if (strcmp(eq + 1, "b") == 0) flag = B_FLAG;
            else if (strcmp(eq + 1, "x") == 0) flag = X_FLAG;
            else if (strcmp(eq + 1, "y") == 0) flag = Y_FLAG;
            if (flag != 0) {
                if (strcmp(line, "cross") == 0) s_face_cross = flag;
                else if (strcmp(line, "circle") == 0) s_face_circle = flag;
                else if (strcmp(line, "square") == 0) s_face_square = flag;
                else if (strcmp(line, "triangle") == 0) s_face_triangle = flag;
            }
        }
        line = next;
    }
    vita_debug_log("[Input] loaded mapping ux0:data/moonlight/vita.conf");
}

void vita_input_set_runtime(
    int swap_xo,
    int front_touchzones,
    int double_tap_sprint,
    int double_tap_ms,
    int keyboard_layout,
    int mapping_enabled,
    int disable_powersave)
{
    s_swap_xo = swap_xo ? 1 : 0;
    s_front_touchzones = front_touchzones ? 1 : 0;
    s_double_tap_sprint = double_tap_sprint ? 1 : 0;
    s_double_tap_ms = double_tap_ms > 0 ? double_tap_ms : 200;
    s_keyboard_layout = keyboard_layout;
    s_mapping_enabled = mapping_enabled ? 1 : 0;
    s_disable_powersave = disable_powersave ? 1 : 0;
    if (!s_mapping_enabled && s_swap_xo) {
        s_face_cross = B_FLAG;
        s_face_circle = A_FLAG;
        s_face_square = X_FLAG;
        s_face_triangle = Y_FLAG;
    } else if (!s_mapping_enabled) {
        s_face_cross = A_FLAG;
        s_face_circle = B_FLAG;
        s_face_square = X_FLAG;
        s_face_triangle = Y_FLAG;
    }
    vita_input_load_mapping();
    if (s_disable_powersave) {
        scePowerSetArmClockFrequency(444);
        scePowerSetBusClockFrequency(222);
        scePowerSetGpuClockFrequency(166);
        scePowerTick(SCE_POWER_TICK_DISABLE_AUTO_SUSPEND);
    }
}

int vita_input_consume_keyboard_request(void)
{
    int requested = s_keyboard_request;
    s_keyboard_request = 0;
    return requested;
}

int vita_input_keyboard_layout(void)
{
    return s_keyboard_layout;
}
