#include "vita_touch.h"

#include <Limelight.h>
#include <psp2/touch.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "debug.h"

#define VITA_TOUCH_WIDTH 960.0f
#define VITA_TOUCH_HEIGHT 544.0f

typedef struct TouchPointState {
    bool active;
    int x;
    int y;
    unsigned char id;
} TouchPointState;

static volatile bool s_active = false;
static int s_mode = 0;
static double s_mouse_multiplier = 1.0;
static bool s_initialized = false;

static SceTouchData s_front;
static TouchPointState s_points[SCE_TOUCH_MAX_REPORT];

static bool s_left_mouse_down = false;
static bool s_two_finger_active = false;
static int s_two_finger_last_y = 0;


static void release_touch_state(void)
{
    unsigned int i;

    if (s_mode == 1) {
        for (i = 0; i < SCE_TOUCH_MAX_REPORT; ++i) {
            if (s_points[i].active) {
                LiSendControllerTouchEvent(
                    0,
                    LI_TOUCH_EVENT_UP,
                    i,
                    (float)s_points[i].x / VITA_TOUCH_WIDTH,
                    (float)s_points[i].y / VITA_TOUCH_HEIGHT,
                    0.0f);
            }
        }
    } else if (s_mode == 3) {
        LiSendTouchEvent(
            LI_TOUCH_EVENT_CANCEL_ALL,
            0,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            LI_ROT_UNKNOWN);
    }

    if (s_left_mouse_down) {
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
        s_left_mouse_down = false;
    }

    s_two_finger_active = false;
    memset(s_points, 0, sizeof(s_points));
}

void vita_touch_init(void)
{
    if (s_initialized) {
        return;
    }

    int front_result = sceTouchSetSamplingState(
        SCE_TOUCH_PORT_FRONT,
        SCE_TOUCH_SAMPLING_STATE_START);
    int back_result = sceTouchSetSamplingState(
        SCE_TOUCH_PORT_BACK,
        SCE_TOUCH_SAMPLING_STATE_START);

    vita_debug_log(
        "[Touch] sampling front=0x%08x back=0x%08x",
        (unsigned int)front_result,
        (unsigned int)back_result);

    memset(&s_front, 0, sizeof(s_front));
    memset(s_points, 0, sizeof(s_points));

    s_initialized = true;
}

void vita_touch_configure(int mode, int mouse_acceleration)
{
    s_mode = mode;
    if (s_mode < 0 || s_mode > 3) {
        s_mode = 0;
    }

    s_mouse_multiplier = 1.0 + (0.01 * (double)mouse_acceleration);
    if (s_mouse_multiplier < 0.1) {
        s_mouse_multiplier = 0.1;
    }
}

void vita_touch_start(void)
{
    vita_touch_init();

    release_touch_state();
    s_active = true;

    vita_debug_log(
        "[Touch] start mode=%d mouse_multiplier=%.2f",
        s_mode,
        s_mouse_multiplier);
}

void vita_touch_stop(void)
{
    if (!s_active) {
        return;
    }

    s_active = false;
    release_touch_state();

    vita_debug_log("[Touch] stop");
}

static void process_ds4_touch(void)
{
    bool present[SCE_TOUCH_MAX_REPORT];
    int x[SCE_TOUCH_MAX_REPORT];
    int y[SCE_TOUCH_MAX_REPORT];
    unsigned int i;

    memset(present, 0, sizeof(present));
    memset(x, 0, sizeof(x));
    memset(y, 0, sizeof(y));

    for (i = 0; i < s_front.reportNum && i < SCE_TOUCH_MAX_REPORT; ++i) {
        unsigned int id = s_front.report[i].id;
        if (id >= SCE_TOUCH_MAX_REPORT) {
            id = i;
        }

        present[id] = true;
        x[id] = (int)s_front.report[i].x * 960 / 1920;
        y[id] = (int)s_front.report[i].y * 544 / 1088;

        if (!s_points[id].active) {
            LiSendControllerTouchEvent(
                0,
                LI_TOUCH_EVENT_DOWN,
                id,
                (float)x[id] / VITA_TOUCH_WIDTH,
                (float)y[id] / VITA_TOUCH_HEIGHT,
                1.0f);
        } else if (s_points[id].x != x[id] || s_points[id].y != y[id]) {
            LiSendControllerTouchEvent(
                0,
                LI_TOUCH_EVENT_MOVE,
                id,
                (float)x[id] / VITA_TOUCH_WIDTH,
                (float)y[id] / VITA_TOUCH_HEIGHT,
                1.0f);
        }
    }

    for (i = 0; i < SCE_TOUCH_MAX_REPORT; ++i) {
        if (s_points[i].active && !present[i]) {
            LiSendControllerTouchEvent(
                0,
                LI_TOUCH_EVENT_UP,
                i,
                (float)s_points[i].x / VITA_TOUCH_WIDTH,
                (float)s_points[i].y / VITA_TOUCH_HEIGHT,
                0.0f);
        }

        s_points[i].active = present[i];
        s_points[i].x = x[i];
        s_points[i].y = y[i];
        s_points[i].id = (unsigned char)i;
    }
}

static void process_absolute_mouse(void)
{
    int finger_count = (int)s_front.reportNum;
    int x = 0;
    int y = 0;

    if (finger_count > 0) {
        x = (int)s_front.report[0].x * 960 / 1920;
        y = (int)s_front.report[0].y * 544 / 1088;
        LiSendMousePositionEvent(
            (short)x,
            (short)y,
            960,
            544);
    }

    if (finger_count == 1) {
        if (!s_left_mouse_down) {
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
            s_left_mouse_down = true;
        }
        s_two_finger_active = false;
        return;
    }

    if (s_left_mouse_down) {
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
        s_left_mouse_down = false;
    }

    if (finger_count >= 2) {
        int avg_y =
            ((int)s_front.report[0].y + (int)s_front.report[1].y) / 2;

        if (!s_two_finger_active) {
            s_two_finger_active = true;
            s_two_finger_last_y = avg_y;
        } else {
            int delta = avg_y - s_two_finger_last_y;
            if (delta != 0) {
                int scroll = (int)lround(
                    ((double)delta / 8.0) * s_mouse_multiplier);
                if (scroll > 127) scroll = 127;
                if (scroll < -127) scroll = -127;
                if (scroll != 0) {
                    LiSendScrollEvent((signed char)scroll);
                }
            }
            s_two_finger_last_y = avg_y;
        }
        return;
    }

    s_two_finger_active = false;
}

static void process_tablet(void)
{
    bool present[SCE_TOUCH_MAX_REPORT];
    unsigned int i;

    memset(present, 0, sizeof(present));

    for (i = 0; i < s_front.reportNum && i < SCE_TOUCH_MAX_REPORT; ++i) {
        unsigned int id = s_front.report[i].id;
        if (id >= SCE_TOUCH_MAX_REPORT) {
            id = i;
        }

        int x = (int)s_front.report[i].x * 960 / 1920;
        int y = (int)s_front.report[i].y * 544 / 1088;
        present[id] = true;

        if (!s_points[id].active) {
            LiSendTouchEvent(
                LI_TOUCH_EVENT_DOWN,
                id,
                (float)x / VITA_TOUCH_WIDTH,
                (float)y / VITA_TOUCH_HEIGHT,
                1.0f,
                0.0f,
                0.0f,
                LI_ROT_UNKNOWN);
        } else if (s_points[id].x != x || s_points[id].y != y) {
            LiSendTouchEvent(
                LI_TOUCH_EVENT_MOVE,
                id,
                (float)x / VITA_TOUCH_WIDTH,
                (float)y / VITA_TOUCH_HEIGHT,
                1.0f,
                0.0f,
                0.0f,
                LI_ROT_UNKNOWN);
        }
    }

    for (i = 0; i < SCE_TOUCH_MAX_REPORT; ++i) {
        if (s_points[i].active && !present[i]) {
            LiSendTouchEvent(
                LI_TOUCH_EVENT_UP,
                i,
                (float)s_points[i].x / VITA_TOUCH_WIDTH,
                (float)s_points[i].y / VITA_TOUCH_HEIGHT,
                0.0f,
                0.0f,
                0.0f,
                LI_ROT_UNKNOWN);
        }

        s_points[i].active = present[i];
        if (!present[i]) {
            s_points[i].x = 0;
            s_points[i].y = 0;
        }
    }
}

void vita_touch_process(void)
{
    if (!s_active || s_mode == 0) {
        return;
    }

    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &s_front, 1) < 0) {
        return;
    }

    switch (s_mode) {
        case 1:
            process_ds4_touch();
            break;
        case 2:
            process_absolute_mouse();
            break;
        case 3:
            process_tablet();
            break;
        default:
            break;
    }
}
