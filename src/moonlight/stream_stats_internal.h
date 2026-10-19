#ifndef VITA_MOONLIGHT_STREAM_STATS_INTERNAL_H
#define VITA_MOONLIGHT_STREAM_STATS_INTERNAL_H

#include "moonlight/types.h"
#include "moonlight/stream_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All writers carry a session token; a stale callback cannot update a new stream. */
uint32_t moonlight_stats_begin(const MoonlightSettings *settings);
void moonlight_stats_end(uint32_t generation);
uint32_t moonlight_stats_configure_video(int format, int width, int height, int fps);
void moonlight_stats_received(uint32_t generation, uint32_t frame_number,
    unsigned int bytes, int idr, unsigned int host_latency,
    uint64_t receive_us, uint64_t enqueue_us);
void moonlight_stats_decode(uint32_t generation, uint64_t call_us,
    unsigned int outputs, int failed, int pacer_drop);
void moonlight_stats_idr_return(uint32_t generation);
void moonlight_stats_prepared(uint32_t generation, uint64_t prepare_us,
    uint64_t vblank_us, unsigned int width, unsigned int height, unsigned int pitch);
void moonlight_stats_queued(uint32_t generation, int replaced);
void moonlight_stats_presented(uint32_t generation, uint64_t queue_us, uint64_t cpu_us);
uint32_t moonlight_stats_configure_audio(int sample_rate, int channels);
void moonlight_stats_audio_decode(uint32_t generation, uint64_t call_us, int failed);
void moonlight_stats_audio_output(uint32_t generation, uint64_t call_us, int failed);
void moonlight_stats_network(uint32_t generation, int valid, uint32_t rtt, uint32_t variation);
int moonlight_stats_snapshot(MoonlightStreamStats *out);

#ifdef __cplusplus
}
#endif
#endif
