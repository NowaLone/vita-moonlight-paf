#include <Limelight.h>
#include <pthread.h>
#include <string.h>

#include "stream_stats_internal.h"

#define STATS_BUCKET_US 1000000ull

enum TimingIndex {
    TIMING_REASSEMBLY,
    TIMING_DECODE,
    TIMING_PREQUEUE,
    TIMING_VBLANK,
    TIMING_PAF_QUEUE,
    TIMING_PAF_CPU,
    TIMING_AUDIO_DECODE,
    TIMING_AUDIO_OUTPUT,
    TIMING_COUNT
};

typedef struct Timing {
    uint64_t samples;
    uint64_t total_us;
    uint64_t maximum_us;
} Timing;

typedef struct Bucket {
    uint64_t received;
    uint64_t decoded;
    uint64_t presented;
    uint64_t queued;
    uint64_t bytes;
    uint64_t missing;
    uint64_t pacer_drops;
    uint64_t replacements;
    uint64_t host_samples;
    uint64_t host_total;
    unsigned int host_min;
    unsigned int host_max;
    Timing timing[TIMING_COUNT];
} Bucket;

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static MoonlightStreamStats s_session;
static Bucket s_current;
static Bucket s_previous;
static uint64_t s_start_us;
static uint64_t s_bucket_start_us;
static uint64_t s_last_video_us;
static uint32_t s_last_frame;
static int s_has_last_frame;

/* Called under s_mutex before recording events, and on reads even during a stall. */
static void advance_window(uint64_t now)
{
    uint64_t steps = (now - s_bucket_start_us) / STATS_BUCKET_US;
    if (steps == 0) {
        return;
    }
    if (steps == 1) {
        s_previous = s_current;
    } else {
        memset(&s_previous, 0, sizeof(s_previous));
    }
    memset(&s_current, 0, sizeof(s_current));
    s_bucket_start_us += steps * STATS_BUCKET_US;
}

static int begin_update(uint32_t generation)
{
    pthread_mutex_lock(&s_mutex);
    if (!generation || generation != s_session.generation ||
        !s_session.active || !s_session.enabled) {
        pthread_mutex_unlock(&s_mutex);
        return 0;
    }
    advance_window(LiGetMicroseconds());
    return 1;
}

static void note_timing(enum TimingIndex index, uint64_t us)
{
    Timing *timing = &s_current.timing[index];
    ++timing->samples;
    timing->total_us += us;
    if (us > timing->maximum_us) {
        timing->maximum_us = us;
    }
}

uint32_t moonlight_stats_begin(const MoonlightSettings *settings)
{
    uint32_t generation;
    uint64_t now = LiGetMicroseconds();

    pthread_mutex_lock(&s_mutex);
    generation = s_session.generation + 1u;
    if (!generation) {
        generation = 1;
    }
    memset(&s_session, 0, sizeof(s_session));
    memset(&s_current, 0, sizeof(s_current));
    memset(&s_previous, 0, sizeof(s_previous));
    s_session.generation = generation;
    s_session.enabled = settings->show_fps != 0;
    s_session.active = 1;
    s_session.width = settings->width;
    s_session.height = settings->height;
    s_session.target_fps = settings->fps;
    s_session.bitrate_kbps = settings->bitrate;
    s_session.frame_pacer = settings->enable_frame_pacer;
    s_session.vblank_wait = settings->enable_vita_vblank_wait;
    s_session.center_region = settings->center_region_only;
    s_start_us = s_bucket_start_us = now;
    s_last_video_us = 0;
    s_last_frame = 0;
    s_has_last_frame = 0;
    pthread_mutex_unlock(&s_mutex);
    return generation;
}

void moonlight_stats_end(uint32_t generation)
{
    pthread_mutex_lock(&s_mutex);
    if (generation && generation == s_session.generation) {
        s_session.active = 0;
        s_session.rtt_valid = 0;
    }
    pthread_mutex_unlock(&s_mutex);
}

uint32_t moonlight_stats_configure_video(int format, int width, int height, int fps)
{
    uint32_t generation;
    pthread_mutex_lock(&s_mutex);
    s_session.video_format = format;
    s_session.width = width;
    s_session.height = height;
    s_session.target_fps = fps;
    generation = s_session.active && s_session.enabled ? s_session.generation : 0;
    pthread_mutex_unlock(&s_mutex);
    return generation;
}

void moonlight_stats_received(uint32_t generation, uint32_t frame_number,
    unsigned int bytes, int idr, unsigned int host_latency,
    uint64_t receive_us, uint64_t enqueue_us)
{
    if (!begin_update(generation)) {
        return;
    }
    ++s_current.received;
    ++s_session.received_frames;
    s_current.bytes += bytes;
    s_last_video_us = LiGetMicroseconds();
    s_session.has_video = 1;
    if (s_has_last_frame) {
        uint32_t delta = frame_number - s_last_frame;
        /* Unsigned subtraction permits wrap; old/duplicate samples cannot move the base. */
        if (delta != 0 && delta < 0x80000000u) {
            s_current.missing += delta - 1u;
            s_session.missing_frames += delta - 1u;
            s_last_frame = frame_number;
        }
    } else {
        s_last_frame = frame_number;
        s_has_last_frame = 1;
    }
    if (idr) {
        ++s_session.idr_frames;
    }
    if (host_latency) {
        if (!s_current.host_samples || host_latency < s_current.host_min) {
            s_current.host_min = host_latency;
        }
        if (host_latency > s_current.host_max) {
            s_current.host_max = host_latency;
        }
        ++s_current.host_samples;
        s_current.host_total += host_latency;
    }
    if (enqueue_us >= receive_us) {
        note_timing(TIMING_REASSEMBLY, enqueue_us - receive_us);
    }
    pthread_mutex_unlock(&s_mutex);
}

void moonlight_stats_decode(uint32_t generation, uint64_t call_us,
    unsigned int outputs, int failed, int pacer_drop)
{
    if (!begin_update(generation)) {
        return;
    }
    note_timing(TIMING_DECODE, call_us);
    if (failed) {
        ++s_session.decode_errors;
    } else {
        s_current.decoded += outputs;
        if (pacer_drop) {
            s_current.pacer_drops += outputs;
            s_session.pacer_drops += outputs;
        }
        if (!outputs) {
            ++s_session.no_output;
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

void moonlight_stats_idr_return(uint32_t generation)
{
    if (begin_update(generation)) {
        ++s_session.idr_returns;
        pthread_mutex_unlock(&s_mutex);
    }
}

void moonlight_stats_prepared(uint32_t generation, uint64_t prepare_us,
    uint64_t vblank_us, unsigned int width, unsigned int height, unsigned int pitch)
{
    if (begin_update(generation)) {
        note_timing(TIMING_PREQUEUE, prepare_us);
        if (s_session.vblank_wait) {
            note_timing(TIMING_VBLANK, vblank_us);
        }
        s_session.output_width = width;
        s_session.output_height = height;
        s_session.output_pitch = pitch;
        pthread_mutex_unlock(&s_mutex);
    }
}

void moonlight_stats_queued(uint32_t generation, int replaced)
{
    if (begin_update(generation)) {
        ++s_current.queued;
        if (replaced) {
            ++s_current.replacements;
            ++s_session.pending_replacements;
        }
        pthread_mutex_unlock(&s_mutex);
    }
}

void moonlight_stats_presented(uint32_t generation, uint64_t queue_us, uint64_t cpu_us)
{
    if (begin_update(generation)) {
        ++s_current.presented;
        note_timing(TIMING_PAF_QUEUE, queue_us);
        note_timing(TIMING_PAF_CPU, cpu_us);
        pthread_mutex_unlock(&s_mutex);
    }
}

void moonlight_stats_network(uint32_t generation, int valid, uint32_t rtt, uint32_t variation)
{
    if (begin_update(generation)) {
        s_session.rtt_valid = valid;
        s_session.rtt_ms = rtt;
        s_session.rtt_variation_ms = variation;
        pthread_mutex_unlock(&s_mutex);
    }
}

static double ratio(uint64_t numerator, uint64_t denominator, double scale)
{
    return denominator ? (double)numerator * scale / (double)denominator : 0.0;
}

uint32_t moonlight_stats_configure_audio(int sample_rate, int channels)
{
    uint32_t generation;
    pthread_mutex_lock(&s_mutex);
    s_session.audio_sample_rate = sample_rate;
    s_session.audio_channels = channels;
    generation = s_session.active && s_session.enabled ? s_session.generation : 0;
    pthread_mutex_unlock(&s_mutex);
    return generation;
}

void moonlight_stats_audio_decode(uint32_t generation, uint64_t call_us, int failed)
{
    if (begin_update(generation)) {
        note_timing(TIMING_AUDIO_DECODE, call_us);
        if (failed) {
            ++s_session.audio_decode_errors;
        }
        pthread_mutex_unlock(&s_mutex);
    }
}

void moonlight_stats_audio_output(uint32_t generation, uint64_t call_us, int failed)
{
    if (begin_update(generation)) {
        note_timing(TIMING_AUDIO_OUTPUT, call_us);
        if (failed) {
            ++s_session.audio_output_errors;
        }
        pthread_mutex_unlock(&s_mutex);
    }
}

static MoonlightTimingStats timing_snapshot(enum TimingIndex index)
{
    Timing *a = &s_current.timing[index];
    Timing *b = &s_previous.timing[index];
    MoonlightTimingStats result;
    result.samples = a->samples + b->samples;
    result.average_ms = ratio(a->total_us + b->total_us, result.samples, 0.001);
    result.maximum_ms = (double)(a->maximum_us > b->maximum_us
        ? a->maximum_us : b->maximum_us) / 1000.0;
    return result;
}

int moonlight_stats_snapshot(MoonlightStreamStats *out)
{
    uint64_t now;
    uint64_t duration;
    uint64_t received;
    uint64_t decoded;
    uint64_t missing;
    uint64_t host_samples;
    unsigned int host_min;

    if (!out) {
        return -1;
    }
    pthread_mutex_lock(&s_mutex);
    now = LiGetMicroseconds();
    advance_window(now);
    *out = s_session;
    duration = now - (s_bucket_start_us == s_start_us
        ? s_start_us : s_bucket_start_us - STATS_BUCKET_US);
    received = s_current.received + s_previous.received;
    decoded = s_current.decoded + s_previous.decoded;
    missing = s_current.missing + s_previous.missing;
    out->video_age_us = out->has_video ? now - s_last_video_us : 0;
    out->incoming_fps = ratio(received, duration, 1000000.0);
    out->decoded_fps = ratio(decoded, duration, 1000000.0);
    out->paf_submit_fps = ratio(s_current.presented + s_previous.presented,
        duration, 1000000.0);
    out->payload_mbps = ratio(s_current.bytes + s_previous.bytes, duration, 8.0);
    out->missing_percent = ratio(missing, received + missing, 100.0);
    out->pacer_drop_percent = ratio(s_current.pacer_drops + s_previous.pacer_drops,
        decoded, 100.0);
    out->replaced_percent = ratio(s_current.replacements + s_previous.replacements,
        s_current.queued + s_previous.queued, 100.0);
    host_samples = s_current.host_samples + s_previous.host_samples;
    out->host_latency_samples = host_samples;
    if (host_samples) {
        host_min = s_current.host_samples ? s_current.host_min : s_previous.host_min;
        if (s_previous.host_samples && s_previous.host_min < host_min) {
            host_min = s_previous.host_min;
        }
        out->host_latency_min_ms = (double)host_min / 10.0;
        out->host_latency_max_ms = (double)(s_current.host_max > s_previous.host_max
            ? s_current.host_max : s_previous.host_max) / 10.0;
        out->host_latency_average_ms = ratio(s_current.host_total + s_previous.host_total,
            host_samples, 0.1);
    }
    out->reassembly = timing_snapshot(TIMING_REASSEMBLY);
    out->decoder_call = timing_snapshot(TIMING_DECODE);
    out->prequeue = timing_snapshot(TIMING_PREQUEUE);
    out->vblank = timing_snapshot(TIMING_VBLANK);
    out->paf_queue = timing_snapshot(TIMING_PAF_QUEUE);
    out->paf_cpu = timing_snapshot(TIMING_PAF_CPU);
    out->audio_decode = timing_snapshot(TIMING_AUDIO_DECODE);
    out->audio_output = timing_snapshot(TIMING_AUDIO_OUTPUT);
    pthread_mutex_unlock(&s_mutex);
    return 0;
}
