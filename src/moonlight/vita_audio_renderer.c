/*
 * Vita audio renderer derived from the original xyzz/vita-moonlight
 * implementation in the "vita" branch.
 */

#include <opus/opus_multistream.h>
#include <psp2/audioout.h>

#include <Limelight.h>

#include "debug.h"

enum {
    VITA_AUDIO_INIT_OK = 0,
    VITA_AUDIO_ERROR_BAD_OPUS = 0x80020001,
    VITA_AUDIO_ERROR_PORT = 0x80020002,
};

#define FRAME_SIZE 240
#define VITA_SAMPLES 960
#define BUFFER_SIZE (2 * VITA_SAMPLES)

static int s_decode_offset = 0;
static int s_port = -1;
static int s_active = 0;
static OpusMSDecoder *s_decoder = NULL;
static short s_buffer[BUFFER_SIZE];

static void vita_audio_cleanup(void)
{
    s_active = 0;
    s_decode_offset = 0;

    if (s_decoder != NULL) {
        opus_multistream_decoder_destroy(s_decoder);
        s_decoder = NULL;
    }

    if (s_port >= 0) {
        int result = sceAudioOutReleasePort(s_port);
        vita_debug_log(
            "[Audio] release port=%d result=0x%08x",
            s_port,
            result);
        s_port = -1;
    }
}

static int vita_audio_init(
    int audio_configuration,
    const POPUS_MULTISTREAM_CONFIGURATION opus_config,
    void *audio_context,
    int ar_flags)
{
    int rc;

    (void)audio_configuration;
    (void)audio_context;
    (void)ar_flags;

    if (!opus_config) {
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }

    s_decode_offset = 0;
    s_active = 0;

    if (s_decoder != NULL || s_port >= 0) {
        vita_audio_cleanup();
    }

    s_decoder = opus_multistream_decoder_create(
        opus_config->sampleRate,
        opus_config->channelCount,
        opus_config->streams,
        opus_config->coupledStreams,
        opus_config->mapping,
        &rc);

    if (rc < 0 || s_decoder == NULL) {
        vita_debug_log(
            "[Audio] opus decoder create failed rc=%d",
            rc);
        s_decoder = NULL;
        return VITA_AUDIO_ERROR_BAD_OPUS;
    }

    s_port = sceAudioOutOpenPort(
        SCE_AUDIO_OUT_PORT_TYPE_MAIN,
        VITA_SAMPLES,
        48000,
        SCE_AUDIO_OUT_MODE_STEREO);

    if (s_port < 0) {
        vita_debug_log(
            "[Audio] sceAudioOutOpenPort failed 0x%08x",
            s_port);
        vita_audio_cleanup();
        return VITA_AUDIO_ERROR_PORT;
    }

    {
        int volume[2] = {
            SCE_AUDIO_VOLUME_0DB,
            SCE_AUDIO_VOLUME_0DB
        };

        sceAudioOutSetVolume(
            s_port,
            SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
            volume);
    }

    vita_debug_log(
        "[Audio] ready rate=%d channels=%d streams=%d coupled=%d port=%d",
        opus_config->sampleRate,
        opus_config->channelCount,
        opus_config->streams,
        opus_config->coupledStreams,
        s_port);

    return VITA_AUDIO_INIT_OK;
}

static void vita_audio_start(void)
{
    s_active = 1;
    s_decode_offset = 0;
    vita_debug_log("[Audio] start");
}

static void vita_audio_stop(void)
{
    s_active = 0;
    vita_debug_log("[Audio] stop");
}

static void vita_audio_decode_and_play_sample(
    char *data,
    int length)
{
    int decoded;

    if (!data || length <= 0 || !s_decoder || s_port < 0 || !s_active) {
        return;
    }

    decoded = opus_multistream_decode(
        s_decoder,
        data,
        length,
        s_buffer + 2 * s_decode_offset,
        FRAME_SIZE,
        0);

    if (decoded > 0) {
        if (decoded != FRAME_SIZE) {
            vita_debug_log(
                "[Audio] unexpected Opus frame size=%d",
                decoded);
            return;
        }

        s_decode_offset += decoded;

        if (s_decode_offset == VITA_SAMPLES) {
            s_decode_offset = 0;
            sceAudioOutOutput(s_port, s_buffer);
        }
    } else {
        vita_debug_log(
            "[Audio] Opus decode error=%d",
            decoded);
    }
}

AUDIO_RENDERER_CALLBACKS audio_callbacks_vita = {
    .init = vita_audio_init,
    .start = vita_audio_start,
    .stop = vita_audio_stop,
    .cleanup = vita_audio_cleanup,
    .decodeAndPlaySample = vita_audio_decode_and_play_sample,
    .capabilities = CAPABILITY_DIRECT_SUBMIT,
};
