#include "vita_motion.h"

#include <Limelight.h>
#include <psp2/motion.h>
#include <psp2/kernel/threadmgr.h>
#include <stdbool.h>
#include <stdint.h>

#include "debug.h"

#define MOTION_SCALE 28.0f

static volatile bool s_active = false;
static volatile bool s_enabled = false;
static volatile uint16_t s_gyro_rate = 0;
static volatile uint16_t s_accel_rate = 0;
static bool s_initialized = false;
static bool s_gyro_thread_started = false;
static bool s_accel_thread_started = false;

static void send_gyro(void)
{
    SceMotionState state;

    if (sceMotionGetState(&state) < 0) {
        return;
    }

    LiSendControllerMotionEvent(
        0,
        LI_MOTION_TYPE_GYRO,
        state.angularVelocity.x * MOTION_SCALE,
        state.angularVelocity.z * MOTION_SCALE,
        state.angularVelocity.y * -MOTION_SCALE);
}

static void send_accel(void)
{
    SceMotionState state;

    if (sceMotionGetState(&state) < 0) {
        return;
    }

    LiSendControllerMotionEvent(
        0,
        LI_MOTION_TYPE_ACCEL,
        state.acceleration.x * MOTION_SCALE,
        state.acceleration.z * MOTION_SCALE,
        state.acceleration.y * -MOTION_SCALE);
}

static int gyro_thread_main(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    while (1) {
        if (s_active && s_enabled && s_gyro_rate != 0) {
            send_gyro();
            sceKernelDelayThread(
                1000000 / (SceUInt)s_gyro_rate);
        } else {
            sceKernelDelayThread(100000);
        }
    }

    return 0;
}

static int accel_thread_main(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    while (1) {
        if (s_active && s_enabled && s_accel_rate != 0) {
            send_accel();
            sceKernelDelayThread(
                1000000 / (SceUInt)s_accel_rate);
        } else {
            sceKernelDelayThread(100000);
        }
    }

    return 0;
}

static void create_thread(
    const char *name,
    int (*entry)(SceSize, void *),
    bool *started)
{
    if (*started) {
        return;
    }

    SceUID thread_id = sceKernelCreateThread(
        name,
        entry,
        0x40,
        0x40000,
        0,
        0,
        NULL);

    if (thread_id < 0) {
        vita_debug_log(
            "[Motion] create thread failed name=%s result=0x%08x",
            name,
            (unsigned int)thread_id);
        return;
    }

    int result = sceKernelStartThread(thread_id, 0, NULL);
    if (result < 0) {
        vita_debug_log(
            "[Motion] start thread failed name=%s result=0x%08x",
            name,
            (unsigned int)result);
        sceKernelDeleteThread(thread_id);
        return;
    }

    *started = true;
}

void vita_motion_init(void)
{
    if (s_initialized) {
        return;
    }

    int result = sceMotionStartSampling();
    if (result < 0 && result != SCE_MOTION_ERROR_ALREADY_SAMPLING) {
        vita_debug_log(
            "[Motion] start sampling failed result=0x%08x",
            (unsigned int)result);
    }

    result = sceMotionReset();
    if (result < 0) {
        vita_debug_log(
            "[Motion] reset failed result=0x%08x",
            (unsigned int)result);
    }

    create_thread(
        "vita_moonlight_gyro",
        gyro_thread_main,
        &s_gyro_thread_started);
    create_thread(
        "vita_moonlight_accel",
        accel_thread_main,
        &s_accel_thread_started);

    s_initialized = true;
}

void vita_motion_start(int enabled)
{
    s_enabled = enabled != 0;

    if (!s_enabled) {
        s_active = false;
        s_gyro_rate = 0;
        s_accel_rate = 0;
        return;
    }

    vita_motion_init();
    s_active = true;
}

void vita_motion_stop(void)
{
    s_active = false;
    s_gyro_rate = 0;
    s_accel_rate = 0;
}

void vita_motion_set_state(
    uint16_t controller,
    uint8_t motion_type,
    uint16_t report_rate_hz)
{
    if (controller != 0 || !s_enabled) {
        return;
    }

    if (motion_type == LI_MOTION_TYPE_GYRO) {
        s_gyro_rate = report_rate_hz;
    } else if (motion_type == LI_MOTION_TYPE_ACCEL) {
        s_accel_rate = report_rate_hz;
    }

    vita_debug_log(
        "[Motion] state controller=%u type=%u rate=%u",
        controller,
        motion_type,
        report_rate_hz);
}
