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
    unsigned int pitch);

void moonlight_video_invalidate(void);
void moonlight_video_release_frame_buffers(void);
unsigned int moonlight_video_get_frame_pool_generation(void);
void moonlight_video_set_stream_options(
    int fps,
    int frame_pacer,
    int vblank_wait,
    int center_region,
    int show_fps,
    int ref_frame_invalidation);
int moonlight_video_show_fps(void);
int moonlight_video_center_region(void);
int moonlight_video_ref_frame_invalidation(void);
unsigned int moonlight_video_presented_fps(void);
unsigned int moonlight_video_target_fps(void);
void moonlight_video_get_requested_size(
    unsigned int *width,
    unsigned int *height);

#ifdef __cplusplus
}
#endif
