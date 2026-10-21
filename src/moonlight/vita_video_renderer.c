#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <psp2/gxm.h>

#include <Limelight.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "app/memory_budget.h"
#include "sps.h"
#include "vita_video_renderer.h"
#include "stream_stats_internal.h"
#ifdef MOONLIGHT_SYSTEM_APP
#include "moonlight/vita_system_video.h"
#endif

static SceAvcdecCtrl *s_decoder;
#ifndef MOONLIGHT_SYSTEM_APP
static SceUID s_decoder_memblock = -1;
#endif
#define VITA_VIDEO_FRAME_COUNT 3
/* Sony SDK error table; this code is absent from VitaSDK's public enum. */
#define VITA_VIDEO_ERROR_NOT_GAME_BUDGET 0x80620840u

static SceUID s_frame_memblocks[VITA_VIDEO_FRAME_COUNT] = { -1, -1, -1 };
static void *s_frame_buffers[VITA_VIDEO_FRAME_COUNT] = { NULL, NULL, NULL };
static int s_frame_gpu_mapped[VITA_VIDEO_FRAME_COUNT] = { 0, 0, 0 };
static unsigned int s_frame_index = 0;
static int s_frame_pool_pending_release;
static unsigned int s_frame_pool_generation;
static SceVideodecQueryInitInfoHwAvcdec *s_init_info;
#ifndef MOONLIGHT_SYSTEM_APP
static SceAvcdecQueryDecoderInfo *s_decoder_info;
#endif
static char *s_decoder_buffer;
static size_t s_decoder_buffer_size;
static unsigned int s_frame_width;
static unsigned int s_frame_height;
enum {
    VITA_SCREEN_WIDTH = 960,
    VITA_SCREEN_HEIGHT = 544,
    FRAMEBUFFER_ALIGNMENT = 256 * 1024
};
static volatile int s_active;
static unsigned int s_decoded_frames;
static int s_target_fps = 60;
static int s_frame_pacer = 1;
static int s_vblank_wait = 0;
static int s_center_region = 0;
static int s_ref_frame_invalidation = 0;
static unsigned int s_requested_width = 960;
static unsigned int s_requested_height = 544;
static uint64_t s_last_present_time;
static uint32_t s_stats_generation;

void moonlight_video_note_queued(uint32_t generation, int replaced)
{
    if (generation) {
        moonlight_stats_queued(generation, replaced);
    }
}

void moonlight_video_note_submitted(uint32_t generation, uint64_t queue_us, uint64_t cpu_us)
{
    if (generation) {
        moonlight_stats_presented(generation, queue_us, cpu_us);
    }
}

static unsigned int align_decoder_resolution(uint64_t value)
{
    return (unsigned int)((value + 15u) & ~((uint64_t)15u));
}

static uint64_t divide_round_up(uint64_t numerator, uint64_t denominator)
{
    return denominator == 0 ? 0 : (numerator + denominator - 1u) / denominator;
}

/*
 * Match the original Vita Moonlight scaling model:
 * - fit mode: contain the whole image inside 960x544;
 * - center-region mode: decode to a cover-sized surface, then copy the
 *   centered 960x544 crop into the start of that same buffer before handing
 *   it to PAF. PAF surfaces do not expose the original renderer's texture
 *   source rectangle, so merely enlarging the plane leaves padding visible.
 *
 * The decoder's frame dimensions and pitch remain the cover-sized values;
 * only the presentation metadata describes the cropped 960x544 image.
 */
static void vita_video_calculate_output_size(
    unsigned int source_width,
    unsigned int source_height)
{
    uint64_t source_is_narrower;
    uint64_t screen_is_narrower;

    if (source_width == 0 || source_height == 0) {
        s_frame_width = VITA_SCREEN_WIDTH;
        s_frame_height = VITA_SCREEN_HEIGHT;
        return;
    }

    source_is_narrower =
        (uint64_t)VITA_SCREEN_WIDTH * source_height;
    screen_is_narrower =
        (uint64_t)VITA_SCREEN_HEIGHT * source_width;

    if (source_is_narrower == screen_is_narrower) {
        s_frame_width = VITA_SCREEN_WIDTH;
        s_frame_height = VITA_SCREEN_HEIGHT;
        return;
    }

    if (source_is_narrower > screen_is_narrower) {
        /* The source is narrower than the Vita screen. */
        if (s_center_region) {
            s_frame_width = VITA_SCREEN_WIDTH;
            s_frame_height = align_decoder_resolution(
                divide_round_up(
                    (uint64_t)VITA_SCREEN_WIDTH * source_height,
                    source_width));
        } else {
            s_frame_width = align_decoder_resolution(
                divide_round_up(
                    (uint64_t)VITA_SCREEN_HEIGHT * source_width,
                    source_height));
            s_frame_height = VITA_SCREEN_HEIGHT;
        }
    } else {
        /* The source is wider than the Vita screen. */
        if (s_center_region) {
            s_frame_width = align_decoder_resolution(
                divide_round_up(
                    (uint64_t)VITA_SCREEN_HEIGHT * source_width,
                    source_height));
            s_frame_height = VITA_SCREEN_HEIGHT;
        } else {
            s_frame_width = VITA_SCREEN_WIDTH;
            s_frame_height = align_decoder_resolution(
                divide_round_up(
                    (uint64_t)VITA_SCREEN_WIDTH * source_height,
                    source_width));
        }
    }
}

void moonlight_video_set_stream_options(
    int fps,
    int frame_pacer,
    int vblank_wait,
    int center_region,
    int ref_frame_invalidation)
{
    if (fps < 1) {
        fps = 60;
    }
    s_target_fps = fps;
    s_frame_pacer = frame_pacer ? 1 : 0;
    s_vblank_wait = vblank_wait ? 1 : 0;
    s_center_region = center_region ? 1 : 0;
    s_ref_frame_invalidation = ref_frame_invalidation ? 1 : 0;
}

int moonlight_video_center_region(void)
{
    return s_center_region;
}

int moonlight_video_ref_frame_invalidation(void)
{
    return s_ref_frame_invalidation;
}

void moonlight_video_get_requested_size(
    unsigned int *width,
    unsigned int *height)
{
    if (width) {
        *width = s_requested_width;
    }
    if (height) {
        *height = s_requested_height;
    }
}

static int vita_video_pacer_should_drop(void)
{
    uint64_t now;
    uint64_t min_interval;

    if (!s_frame_pacer || s_target_fps <= 0) {
        return 0;
    }

    now = sceKernelGetSystemTimeWide();
    min_interval = 1000000ull / (uint64_t)s_target_fps;
    if (s_last_present_time != 0 &&
        now - s_last_present_time < min_interval) {
        return 1;
    }
    return 0;
}

static int vita_video_setup(
    int video_format,
    int width,
    int height,
    int redraw_rate,
    void *context,
    int dr_flags)
{
#ifndef MOONLIGHT_SYSTEM_APP
    SceAvcdecDecoderInfo decoder_mem_info;
    size_t decoder_size;
#endif
    size_t frame_size;
    int result;

    (void)context;
    (void)dr_flags;

#ifdef MOONLIGHT_SYSTEM_APP
    if (s_decoder) {
        vita_debug_log("[Video] nongame decoder retained; restart required");
        return -1;
    }
#endif
    if (width <= 0 || height <= 0) {
        return -1;
    }

    moonlight_log_memory_budget("video-before-setup");
    s_requested_width = (unsigned int)width;
    s_requested_height = (unsigned int)height;
    vita_video_calculate_output_size(
        s_requested_width,
        s_requested_height);

    s_decoder_buffer_size = 128 * 1024 + 64;
    s_decoder_buffer = (char *)malloc(s_decoder_buffer_size);
    if (!s_decoder_buffer) {
        return -1;
    }

    s_init_info = (SceVideodecQueryInitInfoHwAvcdec *)calloc(
        1, sizeof(*s_init_info));
    if (!s_init_info) {
        goto fail;
    }

    s_init_info->size = sizeof(*s_init_info);
    gs_sps_init(width, height);

    s_init_info->horizontal = (unsigned int)(((width + 15) / 16) * 16);
    s_init_info->vertical = (unsigned int)(((height + 15) / 16) * 16);
    s_init_info->numOfRefFrames = 4;
    s_init_info->numOfStreams = 1;

#ifdef MOONLIGHT_SYSTEM_APP
    result = vita_system_video_create(s_init_info, &s_decoder);
    if (result < 0) {
        goto fail;
    }
#else
    result = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, s_init_info);
    if (result < 0) {
        vita_debug_log("[Video] sceVideodecInitLibrary failed 0x%08x", result);
        if ((unsigned int)result == VITA_VIDEO_ERROR_NOT_GAME_BUDGET) {
            vita_debug_log(
                "[Video] SCE_VIDEODEC_ERROR_NOT_GAME_BUDGET: "
                "standard decoder init rejects this application budget");
        }
        goto fail;
    }

    s_decoder_info = (SceAvcdecQueryDecoderInfo *)calloc(
        1, sizeof(*s_decoder_info));
    if (!s_decoder_info) {
        goto fail_library;
    }

    s_decoder_info->horizontal = s_init_info->horizontal;
    s_decoder_info->vertical = s_init_info->vertical;
    s_decoder_info->numOfRefFrames = s_init_info->numOfRefFrames;

    memset(&decoder_mem_info, 0, sizeof(decoder_mem_info));
    result = sceAvcdecQueryDecoderMemSize(
        SCE_VIDEODEC_TYPE_HW_AVCDEC,
        s_decoder_info,
        &decoder_mem_info);
    if (result < 0) {
        vita_debug_log(
            "[Video] sceAvcdecQueryDecoderMemSize failed 0x%08x",
            result);
        goto fail_info;
    }

    decoder_size = (decoder_mem_info.frameMemSize + 0xFFFFF) & ~0xFFFFF;
    s_decoder = (SceAvcdecCtrl *)calloc(1, sizeof(*s_decoder));
    if (!s_decoder) {
        goto fail_info;
    }

    vita_debug_log(
        "[Video] decoder frameMemSize=0x%08x alloc=0x%08x",
        (unsigned int)decoder_mem_info.frameMemSize,
        (unsigned int)decoder_size);
    moonlight_log_memory_budget("video-before-physical-allocation");

    s_decoder->frameBuf.size = decoder_size;
    s_decoder_memblock = sceKernelAllocMemBlock(
        "moonlight_decoder",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
        decoder_size,
        NULL);
    if (s_decoder_memblock < 0) {
        vita_debug_log(
            "[Video] decoder memblock failed 0x%08x",
            s_decoder_memblock);
        goto fail_decoder;
    }

    result = sceKernelGetMemBlockBase(
        s_decoder_memblock,
        &s_decoder->frameBuf.pBuf);
    if (result < 0) {
        vita_debug_log(
            "[Video] sceKernelGetMemBlockBase failed 0x%08x",
            result);
        goto fail_decoder_memblock;
    }

    result = sceAvcdecCreateDecoder(
        SCE_VIDEODEC_TYPE_HW_AVCDEC,
        s_decoder,
        s_decoder_info);
    if (result < 0) {
        vita_debug_log(
            "[Video] sceAvcdecCreateDecoder failed 0x%08x",
            result);
        goto fail_decoder_memblock;
    }

#endif
    /*
     * The output dimensions follow the selected fit/crop mode rather than the
     * source stream dimensions. Reserve enough memory for each RGBA surface,
     * rounded up to the Vita framebuffer alignment.
     */
    frame_size =
        (size_t)s_frame_width * (size_t)s_frame_height * 4u;
    frame_size = (frame_size + FRAMEBUFFER_ALIGNMENT - 1u) &
        ~((size_t)FRAMEBUFFER_ALIGNMENT - 1u);

    /*
     * A new decoder session invalidates every previously queued deferred
     * framebuffer release task.
     */
    ++s_frame_pool_generation;
    if (s_frame_pool_generation == 0) {
        ++s_frame_pool_generation;
    }
    s_frame_pool_pending_release = 0;

    int frame_index;

    /*
     * Keep three external framebuffers, matching the triple-buffered pattern
     * used by libvita2d. Each buffer gets its own PAF Surface and is reused
     * only after two other decoder outputs have been produced.
     */
    s_frame_index = 0;

    for (frame_index = 0;
         frame_index < VITA_VIDEO_FRAME_COUNT;
         ++frame_index) {
        SceKernelAllocMemBlockOpt frame_opt;
        memset(&frame_opt, 0, sizeof(frame_opt));
        frame_opt.size = sizeof(frame_opt);
        frame_opt.attr = 0x00000004;
        frame_opt.alignment = 256 * 1024;

#ifdef MOONLIGHT_SYSTEM_APP
        frame_opt.attr = VITA_SYSTEM_VIDEO_MEM_ATTR;
        frame_opt.alignment = VITA_SYSTEM_VIDEO_MEM_ALIGNMENT;
        /* CDRAM remains occupied by PAF/the suspended game in this profile. */
        s_frame_memblocks[frame_index] = sceKernelAllocMemBlock(
            "moonlight_frame_ddr",
            SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW,
            frame_size,
            &frame_opt);
#else
        s_frame_memblocks[frame_index] = sceKernelAllocMemBlock(
            "moonlight_frame",
            SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
            frame_size,
            &frame_opt);
#endif
        if (s_frame_memblocks[frame_index] < 0) {
            vita_debug_log(
                "[Video] frame[%d] memblock failed 0x%08x",
                frame_index,
                s_frame_memblocks[frame_index]);
            goto fail_frame_pool;
        }

        result = sceKernelGetMemBlockBase(
            s_frame_memblocks[frame_index],
            &s_frame_buffers[frame_index]);
        if (result < 0) {
            vita_debug_log(
                "[Video] frame[%d] base failed 0x%08x",
                frame_index,
                result);
            goto fail_frame_pool;
        }

        result = sceGxmMapMemory(
            s_frame_buffers[frame_index],
            frame_size,
            SCE_GXM_MEMORY_ATTRIB_READ |
            SCE_GXM_MEMORY_ATTRIB_WRITE);
        if (result < 0) {
            vita_debug_log(
                "[Video] frame[%d] sceGxmMapMemory failed 0x%08x",
                frame_index,
                result);
            goto fail_frame_pool;
        }
        s_frame_gpu_mapped[frame_index] = 1;

        memset(s_frame_buffers[frame_index], 0, frame_size);

        vita_debug_log(
            "[Video] frame[%d] buffer=%p mapped",
            frame_index,
            s_frame_buffers[frame_index]);
    }

    s_active = 1;
    moonlight_log_memory_budget("video-ready");
    s_decoded_frames = 0;
    s_last_present_time = 0;
    s_stats_generation = moonlight_stats_configure_video(
        video_format, width, height, redraw_rate);
#ifdef CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC
    decoder_callbacks_vita.capabilities =
        CAPABILITY_DIRECT_SUBMIT |
        CAPABILITY_SLICES_PER_FRAME(2) |
        (s_ref_frame_invalidation
            ? CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC
            : 0);
#endif

    vita_debug_log(
        "[Video] decoder ready %ux%u",
        s_frame_width,
        s_frame_height);
    return 0;

fail_frame_pool:
    for (frame_index = 0;
         frame_index < VITA_VIDEO_FRAME_COUNT;
         ++frame_index) {
        if (s_frame_gpu_mapped[frame_index] &&
            s_frame_buffers[frame_index]) {
            sceGxmUnmapMemory(s_frame_buffers[frame_index]);
            s_frame_gpu_mapped[frame_index] = 0;
        }
        if (s_frame_memblocks[frame_index] >= 0) {
            sceKernelFreeMemBlock(s_frame_memblocks[frame_index]);
            s_frame_memblocks[frame_index] = -1;
        }
        s_frame_buffers[frame_index] = NULL;
    }
#ifdef MOONLIGHT_SYSTEM_APP
    vita_system_video_destroy();
    s_decoder = NULL;
#else
    sceAvcdecDeleteDecoder(s_decoder);
fail_decoder_memblock:
    sceKernelFreeMemBlock(s_decoder_memblock);
    s_decoder_memblock = -1;
fail_decoder:
    free(s_decoder);
    s_decoder = NULL;
fail_info:
    free(s_decoder_info);
    s_decoder_info = NULL;
fail_library:
    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
#endif
fail:
    /* SPS/init-info also exist when library initialization itself fails. */
    gs_sps_stop();
    free(s_init_info);
    s_init_info = NULL;
    free(s_decoder_buffer);
    s_decoder_buffer = NULL;
    s_decoder_buffer_size = 0;
    moonlight_log_memory_budget("video-setup-failed");
    return -1;
}

static void vita_video_cleanup(void)
{
    s_active = 0;

    moonlight_video_invalidate();

    if (s_decoder) {
#ifdef MOONLIGHT_SYSTEM_APP
        if (vita_system_video_destroy() < 0) {
            /* Runtime ownership is uncertain: retain buffers until restart. */
            vita_debug_log("[Video] nongame cleanup retained resources");
            return;
        }
#else
        sceAvcdecDeleteDecoder(s_decoder);
#endif
        s_decoder = NULL;
    }

    gs_sps_stop();

    /*
     * PAF owns graph::Surface objects which reference these framebuffers.
     * Limelight cleanup runs before the STREAM_STOPPED event reaches the
     * PAF main thread, so keep the GPU-visible buffers alive until that page
     * has been detached and destroyed.
     */
    s_frame_pool_pending_release = 1;

#ifndef MOONLIGHT_SYSTEM_APP
    if (s_decoder_memblock >= 0) {
        sceKernelFreeMemBlock(s_decoder_memblock);
        s_decoder_memblock = -1;
    }

    free(s_decoder_info);
    s_decoder_info = NULL;
#endif

    free(s_init_info);
    s_init_info = NULL;

#ifndef MOONLIGHT_SYSTEM_APP
    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
#endif

    free(s_decoder_buffer);
    s_decoder_buffer = NULL;
    s_decoder_buffer_size = 0;

    vita_debug_log(
        "[Video] decoded frames=%u",
        s_decoded_frames);
}

static void vita_video_release_frame_buffers_internal(void)
{
    int frame_index;

    if (!s_frame_pool_pending_release) {
        return;
    }

    for (frame_index = 0;
         frame_index < VITA_VIDEO_FRAME_COUNT;
         ++frame_index) {
        if (s_frame_gpu_mapped[frame_index] &&
            s_frame_buffers[frame_index]) {
            int result = sceGxmUnmapMemory(s_frame_buffers[frame_index]);
            vita_debug_log(
                "[Video] frame[%d] release unmap result=0x%08x",
                frame_index,
                result);
            s_frame_gpu_mapped[frame_index] = 0;
        }

        if (s_frame_memblocks[frame_index] >= 0) {
            sceKernelFreeMemBlock(s_frame_memblocks[frame_index]);
            s_frame_memblocks[frame_index] = -1;
        }

        s_frame_buffers[frame_index] = NULL;
    }

    s_frame_index = 0;
    s_frame_pool_pending_release = 0;

    vita_debug_log("[Video] deferred frame buffers released");
    moonlight_log_memory_budget("video-framebuffers-released");
}

void moonlight_video_release_frame_buffers(void)
{
    vita_video_release_frame_buffers_internal();
}

unsigned int moonlight_video_get_frame_pool_generation(void)
{
    return s_frame_pool_generation;
}

#ifdef MOONLIGHT_STARTUP_DISCONNECT_TEST
/* Read only after the owner's allocation checkpoint or completed host teardown.
 * Count any still-owned slot, including partially cleared allocation/mapping state. */
int moonlight_video_test_owned_frame_buffers(void)
{
    int count = 0;
    for (int i = 0; i < VITA_VIDEO_FRAME_COUNT; ++i) {
        if (s_frame_memblocks[i] >= 0 || s_frame_buffers[i] || s_frame_gpu_mapped[i]) {
            count = count + 1;
        }
    }
    return count;
}
#endif

static int vita_video_need_idr(uint32_t generation)
{
    if (generation) {
        moonlight_stats_idr_return(generation);
    }
    return DR_NEED_IDR;
}

static int vita_video_submit(PDECODE_UNIT decode_unit)
{
    SceAvcdecAu au;
    SceAvcdecArrayPicture array_picture;
    SceAvcdecPicture picture;
    SceAvcdecPicture *pictures;
    PLENTRY entry;
    size_t required;
    size_t length = 0;
    int result;
    uint32_t generation = s_stats_generation;
    uint64_t decode_start_us = 0;
    uint64_t decode_end_us = 0;
    uint64_t vblank_us = 0;
    int pacer_drop = 0;

    if (generation && decode_unit) {
        moonlight_stats_received(generation, (uint32_t)decode_unit->frameNumber,
            (unsigned int)decode_unit->fullLength, decode_unit->frameType == FRAME_TYPE_IDR,
            decode_unit->frameHostProcessingLatency,
            decode_unit->receiveTimeUs, decode_unit->enqueueTimeUs);
    }

    if (!decode_unit || !s_decoder ||
        !s_frame_buffers[s_frame_index]) {
        return vita_video_need_idr(generation);
    }

    required = (size_t)decode_unit->fullLength + 64;
    if (required > s_decoder_buffer_size) {
        char *new_buffer = (char *)realloc(
            s_decoder_buffer,
            required);
        if (!new_buffer) {
            return vita_video_need_idr(generation);
        }
        s_decoder_buffer = new_buffer;
        s_decoder_buffer_size = required;
    }

    entry = decode_unit->bufferList;
    while (entry) {
        if (entry->length > 0) {
            if (entry->bufferType == BUFFER_TYPE_SPS) {
                gs_sps_fix(
                    entry,
                    GS_SPS_BITSTREAM_FIXUP,
                    (uint8_t *)s_decoder_buffer,
                    (uint32_t *)&length);
            } else {
                memcpy(
                    s_decoder_buffer + length,
                    entry->data,
                    (size_t)entry->length);
                length += (size_t)entry->length;
            }
        }
        entry = entry->next;
    }

    if (length == 0 || length > (size_t)decode_unit->fullLength + 64) {
        vita_debug_log(
            "[Video] invalid assembled decode unit length=%u full=%u",
            (unsigned int)length,
            decode_unit->fullLength);
        return vita_video_need_idr(generation);
    }

    memset(&au, 0, sizeof(au));
    memset(&array_picture, 0, sizeof(array_picture));
    memset(&picture, 0, sizeof(picture));

    pictures = &picture;
    array_picture.numOfElm = 1;
    array_picture.pPicture = &pictures;

    picture.size = sizeof(picture);
    picture.frame.pixelType = 0;
    picture.frame.framePitch = s_frame_width;
    picture.frame.frameWidth = s_frame_width;
    picture.frame.frameHeight = s_frame_height;
    picture.frame.pPicture[0] = s_frame_buffers[s_frame_index];

    au.es.pBuf = s_decoder_buffer;
    au.es.size = (uint32_t)length;
    au.dts.lower = 0xFFFFFFFF;
    au.dts.upper = 0xFFFFFFFF;
    au.pts.lower = 0xFFFFFFFF;
    au.pts.upper = 0xFFFFFFFF;

    if (generation) {
        decode_start_us = LiGetMicroseconds();
    }
    result = sceAvcdecDecode(s_decoder, &au, &array_picture);
    if (generation) {
        decode_end_us = LiGetMicroseconds();
    }
    if (result >= 0 && array_picture.numOfOutput > 0 && s_active) {
        pacer_drop = vita_video_pacer_should_drop();
    }
    if (generation) {
        moonlight_stats_decode(generation, decode_end_us - decode_start_us,
            result < 0 ? 0 : array_picture.numOfOutput, result < 0, pacer_drop);
    }
    if (result < 0) {
        vita_debug_log(
            "[Video] sceAvcdecDecode failed frame=%d len=%u err=0x%08x",
            decode_unit->frameNumber,
            (unsigned int)length,
            result);
        return vita_video_need_idr(generation);
    }

    if (array_picture.numOfOutput > 0 && s_active) {
        if (pacer_drop) {
            ++s_decoded_frames;
            return DR_OK;
        }
        if (s_vblank_wait) {
            uint64_t wait_start_us = generation ? LiGetMicroseconds() : 0;
            sceDisplayWaitVblankStart();
            if (generation) {
                vblank_us = LiGetMicroseconds() - wait_start_us;
            }
        }
        s_last_present_time = sceKernelGetSystemTimeWide();
        if (s_center_region) {
            unsigned int crop_x =
                (s_frame_width - VITA_SCREEN_WIDTH) / 2u;
            unsigned int crop_y =
                (s_frame_height - VITA_SCREEN_HEIGHT) / 2u;
            unsigned int row;
            unsigned char *buffer =
                (unsigned char *)s_frame_buffers[s_frame_index];

            /*
             * The decoder buffer is larger than the visible crop in at most
             * one dimension. Move the central rectangle to the start of the
             * same buffer, top-to-bottom; destinations are always before
             * their sources, so no additional framebuffer allocation is
             * required. Triple buffering keeps this buffer out of PAF's
             * current presentation while the decoder reuses it.
             */
            for (row = 0; row < VITA_SCREEN_HEIGHT; ++row) {
                unsigned char *destination =
                    buffer + (size_t)row * VITA_SCREEN_WIDTH * 4u;
                const unsigned char *source =
                    buffer +
                    ((size_t)(row + crop_y) * s_frame_width + crop_x) * 4u;
                memmove(
                    destination,
                    source,
                    (size_t)VITA_SCREEN_WIDTH * 4u);
            }

            if (generation) {
                moonlight_stats_prepared(generation, LiGetMicroseconds() - decode_end_us,
                    vblank_us, VITA_SCREEN_WIDTH, VITA_SCREEN_HEIGHT, VITA_SCREEN_WIDTH);
            }
            moonlight_video_present(
                s_frame_buffers[s_frame_index],
                VITA_SCREEN_WIDTH,
                VITA_SCREEN_HEIGHT,
                VITA_SCREEN_WIDTH,
                generation);
        } else {
            if (generation) {
                moonlight_stats_prepared(generation, LiGetMicroseconds() - decode_end_us,
                    vblank_us, s_frame_width, s_frame_height, s_frame_width);
            }
            moonlight_video_present(
                s_frame_buffers[s_frame_index],
                s_frame_width,
                s_frame_height,
                s_frame_width,
                generation);
        }

        s_frame_index =
            (s_frame_index + 1) % VITA_VIDEO_FRAME_COUNT;
    }

    if (array_picture.numOfOutput > 0 && s_active) {
        ++s_decoded_frames;
        if ((s_decoded_frames % 60) == 1) {
            vita_debug_log(
                "[Video] decoded frame=%u number=%d",
                s_decoded_frames,
                decode_unit->frameNumber);
        }
    }

    return DR_OK;
}

DECODER_RENDERER_CALLBACKS decoder_callbacks_vita = {
    .setup = vita_video_setup,
    .start = NULL,
    .stop = NULL,
    .cleanup = vita_video_cleanup,
    .submitDecodeUnit = vita_video_submit,
    .capabilities = CAPABILITY_DIRECT_SUBMIT |
                    CAPABILITY_SLICES_PER_FRAME(2)
};
