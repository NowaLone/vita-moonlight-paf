#include "moonlight/vita_system_video.h"

#include <psp2/kernel/sysmem.h>
#include <stdint.h>
#include <string.h>

#include "app/memory_budget.h"
#include "debug.h"

/*
 * Missing VitaSDK declarations. Layout/copy sizes verified in the supplied
 * SceAvcodec/CodecEngineWrapper ELFs; see docs/SYSTEM_APP_MODE_RESEARCH.md.
 * No firmware code or private SDK header is linked into the application.
 */
typedef struct VitaVideodecLibraryCtrl {
    SceAvcdecBuf mem_buf;
    SceUID mem_buf_uid;
    uint32_t context_address;
    uint32_t context_size;
} VitaVideodecLibraryCtrl;
VITASDK_BUILD_ASSERT_EQ(0x14, VitaVideodecLibraryCtrl);

int sceVideodecQueryMemSizeNongameapp(
    SceVideodecType codec, const SceVideodecQueryInitInfo *info,
    uint32_t *memory_size);
int sceVideodecInitLibraryWithUnmapMemNongameapp(
    SceVideodecType codec, VitaVideodecLibraryCtrl *ctrl,
    const SceVideodecQueryInitInfo *info);
int sceAvcdecQueryDecoderMemSizeNongameapp(
    SceVideodecType codec, const SceAvcdecQueryDecoderInfo *query,
    SceAvcdecDecoderInfo *info);
int sceAvcdecCreateDecoderNongameapp(
    SceVideodecType codec, SceAvcdecCtrl *decoder,
    const SceAvcdecQueryDecoderInfo *query);
SceUID sceCodecEngineOpenUnmapMemBlock(void *base, uint32_t size);
int sceCodecEngineCloseUnmapMemBlock(SceUID uid);
uint32_t sceCodecEngineAllocMemoryFromUnmapMemBlock(
    SceUID uid, uint32_t size, uint32_t alignment);
int sceCodecEngineFreeMemoryFromUnmapMemBlock(SceUID uid, uint32_t address);

static SceUID s_arena = -1;
static SceUID s_pool = -1;
static uint32_t s_library_context;
static uint32_t s_decoder_context;
static int s_library_initialized;
static int s_decoder_created;
static SceAvcdecCtrl s_decoder;

static uint32_t round_allocation(uint32_t size, uint32_t alignment)
{
    /* Bound arithmetic/requests before rounding, independently of firmware. */
    if (size == 0 || size > 64u * 1024u * 1024u - (alignment - 1u)) {
        return 0;
    }
    return (size + alignment - 1u) & ~(alignment - 1u);
}

int vita_system_video_destroy(void)
{
    int result;

    /* Keep backing storage alive if the runtime still owns a decoder/context. */
    if (s_decoder_created) {
        result = sceAvcdecDeleteDecoder(&s_decoder);
        if (result < 0) {
            vita_debug_log("[Video] nongame delete failed 0x%08x", result);
            return result;
        }
        s_decoder_created = 0;
    }
    if (s_library_initialized) {
        result = sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
        if (result < 0) {
            vita_debug_log("[Video] nongame term failed 0x%08x", result);
            return result;
        }
        s_library_initialized = 0;
    }
    if (s_decoder_context) {
        result = sceCodecEngineFreeMemoryFromUnmapMemBlock(
            s_pool, s_decoder_context);
        if (result < 0) {
            vita_debug_log("[Video] nongame frame-store free failed 0x%08x", result);
            return result;
        }
        s_decoder_context = 0;
    }
    if (s_library_context) {
        result = sceCodecEngineFreeMemoryFromUnmapMemBlock(
            s_pool, s_library_context);
        if (result < 0) {
            vita_debug_log("[Video] nongame library-context free failed 0x%08x", result);
            return result;
        }
        s_library_context = 0;
    }
    if (s_pool >= 0) {
        result = sceCodecEngineCloseUnmapMemBlock(s_pool);
        if (result < 0) {
            vita_debug_log("[Video] nongame pool close failed 0x%08x", result);
            return result;
        }
        s_pool = -1;
    }
    if (s_arena >= 0) {
        result = sceKernelFreeMemBlock(s_arena);
        if (result < 0) {
            vita_debug_log("[Video] nongame arena free failed 0x%08x", result);
            return result;
        }
        s_arena = -1;
    }
    memset(&s_decoder, 0, sizeof(s_decoder));
    return 0;
}

int vita_system_video_create(
    const SceVideodecQueryInitInfoHwAvcdec *init_info,
    SceAvcdecCtrl **decoder)
{
    SceVideodecQueryInitInfo info;
    SceAvcdecQueryDecoderInfo query;
    SceAvcdecDecoderInfo decoder_info;
    VitaVideodecLibraryCtrl library_ctrl;
    SceKernelAllocMemBlockOpt opt;
    uint32_t library_size = 0;
    uint32_t context_size;
    uint32_t frame_store_size;
    uint32_t arena_size;
    void *base = NULL;
    int result;

    *decoder = NULL;
    if (s_arena >= 0) {
        vita_debug_log("[Video] nongame previous resources still retained");
        return (int)SCE_VIDEODEC_ERROR_INVALID_STATE;
    }

    /* The syscall copies the whole union, even though hwAvc.size is 20. */
    memset(&info, 0, sizeof(info));
    info.hwAvc = *init_info;
    memset(&query, 0, sizeof(query));
    query.horizontal = init_info->horizontal;
    query.vertical = init_info->vertical;
    query.numOfRefFrames = init_info->numOfRefFrames;
    memset(&decoder_info, 0, sizeof(decoder_info));

    result = sceVideodecQueryMemSizeNongameapp(
        SCE_VIDEODEC_TYPE_HW_AVCDEC, &info, &library_size);
    vita_debug_log("[Video] nongame library query result=0x%08x bytes=%u",
        result, (unsigned int)library_size);
    if (result < 0) {
        return result;
    }
    result = sceAvcdecQueryDecoderMemSizeNongameapp(
        SCE_VIDEODEC_TYPE_HW_AVCDEC, &query, &decoder_info);
    vita_debug_log("[Video] nongame decoder query result=0x%08x bytes=%u",
        result, (unsigned int)decoder_info.frameMemSize);
    if (result < 0) {
        return result;
    }

    context_size = round_allocation(library_size, 0x40000u);
    frame_store_size = round_allocation(
        decoder_info.frameMemSize, VITA_SYSTEM_VIDEO_MEM_ALIGNMENT);
    /* Reserve one extra MiB for pool metadata and suballocation alignment. */
    arena_size = round_allocation(context_size, VITA_SYSTEM_VIDEO_MEM_ALIGNMENT) +
        frame_store_size + VITA_SYSTEM_VIDEO_MEM_ALIGNMENT;
    if (!context_size || !frame_store_size || arena_size > 64u * 1024u * 1024u) {
        return (int)SCE_VIDEODEC_ERROR_OUT_OF_MEMORY;
    }
    vita_debug_log("[Video] nongame arena bytes=%u library=%u decoder=%u",
        (unsigned int)arena_size, (unsigned int)context_size,
        (unsigned int)frame_store_size);
    moonlight_log_memory_budget("video-before-nongame-arena");
    memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt);
    opt.attr = VITA_SYSTEM_VIDEO_MEM_ATTR;
    opt.alignment = VITA_SYSTEM_VIDEO_MEM_ALIGNMENT;
    s_arena = sceKernelAllocMemBlock("moonlight_codec",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, arena_size, &opt);
    vita_debug_log("[Video] nongame arena alloc result=0x%08x", s_arena);
    if (s_arena < 0) {
        result = s_arena;
        goto fail;
    }
    result = sceKernelGetMemBlockBase(s_arena, &base);
    if (result < 0) {
        goto fail;
    }
    s_pool = sceCodecEngineOpenUnmapMemBlock(base, arena_size);
    vita_debug_log("[Video] nongame pool open result=0x%08x", s_pool);
    if (s_pool < 0) {
        result = s_pool;
        goto fail;
    }
    /* Open unmaps base: never memset/read/write that arena from the CPU. */
    s_library_context = sceCodecEngineAllocMemoryFromUnmapMemBlock(
        s_pool, context_size, 0x40000u);
    s_decoder_context = sceCodecEngineAllocMemoryFromUnmapMemBlock(
        s_pool, frame_store_size, VITA_SYSTEM_VIDEO_MEM_ALIGNMENT);
    vita_debug_log("[Video] nongame contexts library=0x%08x decoder=0x%08x",
        (unsigned int)s_library_context, (unsigned int)s_decoder_context);
    if (!s_library_context || !s_decoder_context) {
        result = (int)SCE_VIDEODEC_ERROR_OUT_OF_MEMORY;
        goto fail;
    }
    memset(&library_ctrl, 0, sizeof(library_ctrl));
    library_ctrl.context_address = s_library_context;
    library_ctrl.context_size = context_size;
    result = sceVideodecInitLibraryWithUnmapMemNongameapp(
        SCE_VIDEODEC_TYPE_HW_AVCDEC, &library_ctrl, &info);
    vita_debug_log("[Video] nongame explicit library init result=0x%08x", result);
    if (result < 0) {
        goto fail;
    }
    s_library_initialized = 1;
    memset(&s_decoder, 0, sizeof(s_decoder));
    s_decoder.frameBuf.pBuf = (void *)(uintptr_t)s_decoder_context;
    s_decoder.frameBuf.size = frame_store_size;
    result = sceAvcdecCreateDecoderNongameapp(
        SCE_VIDEODEC_TYPE_HW_AVCDEC, &s_decoder, &query);
    vita_debug_log("[Video] nongame explicit decoder create result=0x%08x", result);
    if (result < 0) {
        goto fail;
    }
    s_decoder_created = 1;
    *decoder = &s_decoder;
    moonlight_log_memory_budget("video-nongame-decoder-ready");
    return 0;

fail:
    vita_debug_log("[Video] nongame setup failed 0x%08x", result);
    vita_system_video_destroy();
    return result;
}
