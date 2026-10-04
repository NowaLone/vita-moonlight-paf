#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <psp2/gxm.h>

#include <Limelight.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "sps.h"
#include "vita_video_renderer.h"

static SceAvcdecCtrl *s_decoder;
static SceUID s_decoder_memblock = -1;
#define VITA_VIDEO_FRAME_COUNT 3

static SceUID s_frame_memblocks[VITA_VIDEO_FRAME_COUNT] = { -1, -1, -1 };
static void *s_frame_buffers[VITA_VIDEO_FRAME_COUNT] = { NULL, NULL, NULL };
static int s_frame_gpu_mapped[VITA_VIDEO_FRAME_COUNT] = { 0, 0, 0 };
static unsigned int s_frame_index = 0;
static int s_frame_pool_pending_release;
static SceVideodecQueryInitInfoHwAvcdec *s_init_info;
static SceAvcdecQueryDecoderInfo *s_decoder_info;
static char *s_decoder_buffer;
static size_t s_decoder_buffer_size;
static unsigned int s_frame_width;
static unsigned int s_frame_height;
static const unsigned int s_output_width = 960;
static const unsigned int s_output_height = 544;
static volatile int s_active;
static unsigned int s_decoded_frames;

static int vita_video_setup(
    int video_format,
    int width,
    int height,
    int redraw_rate,
    void *context,
    int dr_flags)
{
    SceAvcdecDecoderInfo decoder_mem_info;
    size_t decoder_size;
    size_t frame_size;
    int result;

    (void)video_format;
    (void)redraw_rate;
    (void)context;
    (void)dr_flags;

    if (width <= 0 || height <= 0) {
        return -1;
    }

    s_frame_width = s_output_width;
    s_frame_height = s_output_height;

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

    result = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, s_init_info);
    if (result < 0) {
        vita_debug_log("[Video] sceVideodecInitLibrary failed 0x%08x", result);
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

    /*
     * The original vita-moonlight renderer uses a 960x544 framebuffer
     * (2 MiB CDRAM) even when the stream itself is 1280x720. The decoder
     * writes its output directly into that display-sized buffer.
     */
    frame_size = 2 * 1024 * 1024;

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

        s_frame_memblocks[frame_index] = sceKernelAllocMemBlock(
            "moonlight_frame",
            SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
            frame_size,
            &frame_opt);
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
    s_decoded_frames = 0;

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
    free(s_init_info);
    s_init_info = NULL;
fail:
    free(s_decoder_buffer);
    s_decoder_buffer = NULL;
    s_decoder_buffer_size = 0;
    return -1;
}

static void vita_video_cleanup(void)
{
    s_active = 0;

    moonlight_video_invalidate();

    if (s_decoder) {
        sceAvcdecDeleteDecoder(s_decoder);
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

    if (s_decoder_memblock >= 0) {
        sceKernelFreeMemBlock(s_decoder_memblock);
        s_decoder_memblock = -1;
    }

    free(s_decoder_info);
    s_decoder_info = NULL;

    free(s_init_info);
    s_init_info = NULL;

    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);

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
}

void moonlight_video_release_frame_buffers(void)
{
    vita_video_release_frame_buffers_internal();
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

    if (!decode_unit || !s_decoder ||
        !s_frame_buffers[s_frame_index]) {
        return DR_NEED_IDR;
    }

    required = (size_t)decode_unit->fullLength + 64;
    if (required > s_decoder_buffer_size) {
        char *new_buffer = (char *)realloc(
            s_decoder_buffer,
            required);
        if (!new_buffer) {
            return DR_NEED_IDR;
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
        return DR_NEED_IDR;
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

    result = sceAvcdecDecode(s_decoder, &au, &array_picture);
    if (result < 0) {
        vita_debug_log(
            "[Video] sceAvcdecDecode failed frame=%d len=%u err=0x%08x",
            decode_unit->frameNumber,
            (unsigned int)length,
            result);
        return DR_NEED_IDR;
    }

    if (array_picture.numOfOutput > 0 && s_active) {
        vita_debug_log(
            "[Video] present frame=%u buffer=%p slot=%u",
            s_decoded_frames + 1,
            s_frame_buffers[s_frame_index],
            s_frame_index);
        moonlight_video_present(
            s_frame_buffers[s_frame_index],
            s_frame_width,
            s_frame_height,
            s_frame_width);

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
