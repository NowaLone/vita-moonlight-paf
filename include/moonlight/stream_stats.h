#ifndef VITA_MOONLIGHT_STREAM_STATS_H
#define VITA_MOONLIGHT_STREAM_STATS_H

#include <stdint.h>

/* Read-only snapshot. Rates/timings cover the current and previous 1 s buckets. */
typedef struct MoonlightTimingStats {
    uint64_t samples;
    double average_ms;
    double maximum_ms;
} MoonlightTimingStats;

typedef struct MoonlightStreamStats {
    uint32_t generation;
    int enabled;
    int active;
    int width;
    int height;
    int video_format;
    int target_fps;
    int bitrate_kbps;
    int frame_pacer;
    int vblank_wait;
    int center_region;
    unsigned int output_width;
    unsigned int output_height;
    unsigned int output_pitch;
    int has_video;
    uint64_t video_age_us;
    double incoming_fps;
    double decoded_fps;
    double paf_submit_fps;
    double payload_mbps;
    double missing_percent;
    double pacer_drop_percent;
    double replaced_percent;
    uint64_t received_frames;
    uint64_t missing_frames;
    uint64_t pacer_drops;
    uint64_t pending_replacements;
    uint64_t decode_errors;
    uint64_t idr_returns;
    uint64_t idr_frames;
    uint64_t no_output;
    uint64_t host_latency_samples;
    double host_latency_min_ms;
    double host_latency_max_ms;
    double host_latency_average_ms;
    int rtt_valid;
    uint32_t rtt_ms;
    uint32_t rtt_variation_ms;
    MoonlightTimingStats reassembly;
    MoonlightTimingStats decoder_call;
    MoonlightTimingStats prequeue;
    MoonlightTimingStats vblank;
    MoonlightTimingStats paf_queue;
    MoonlightTimingStats paf_cpu;
    int audio_sample_rate;
    int audio_channels;
    uint64_t audio_decode_errors;
    uint64_t audio_output_errors;
    MoonlightTimingStats audio_decode;
    MoonlightTimingStats audio_output;
} MoonlightStreamStats;

#endif
