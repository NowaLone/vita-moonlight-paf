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

#ifdef __cplusplus
}
#endif
