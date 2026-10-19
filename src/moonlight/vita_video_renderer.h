#pragma once
#include <Limelight.h>

#ifdef __cplusplus
extern "C" {
#endif

extern DECODER_RENDERER_CALLBACKS decoder_callbacks_vita;

void moonlight_video_present(
    void *buffer,
    unsigned int width,
    unsigned int height,
    unsigned int pitch,
    uint32_t stats_generation);

/* Presentation hooks carry the frame's session token; they do not read transport state. */
void moonlight_video_note_queued(uint32_t generation, int replaced);
void moonlight_video_note_submitted(uint32_t generation, uint64_t queue_us, uint64_t cpu_us);

void moonlight_video_invalidate(void);
void moonlight_video_release_frame_buffers(void);
unsigned int moonlight_video_get_frame_pool_generation(void);
void moonlight_video_set_stream_options(
    int fps,
    int frame_pacer,
    int vblank_wait,
    int center_region,
    int ref_frame_invalidation);
int moonlight_video_center_region(void);
int moonlight_video_ref_frame_invalidation(void);
void moonlight_video_get_requested_size(
    unsigned int *width,
    unsigned int *height);

#ifdef __cplusplus
}
#endif
