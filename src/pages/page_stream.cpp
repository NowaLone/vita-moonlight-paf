#include <paf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>

#include <Limelight.h>

#ifndef KEY_ACTION_DOWN
#define KEY_ACTION_DOWN 0x03
#endif
#ifndef KEY_ACTION_UP
#define KEY_ACTION_UP 0x04
#endif

#include "debug.h"
#include "app/moonlight_app.h"
#include "pages/page_stream.h"
#include "../moonlight/vita_input.h"
#include "../moonlight/vita_video_renderer.h"

using namespace paf;

namespace page {

Stream *Stream::s_instance = NULL;
Stream::Frame Stream::s_pending_frame = { NULL, 0, 0, 0, false, 0, 0 };
unsigned int Stream::s_release_delay = 0;

static void stream_power_tick_task(void *)
{
    /*
     * Match NetStream's PowerTick_All behavior while the stream page is active:
     * reset standby, display-off and dimming idle timers during video playback.
     */
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
}

static void set_stream_power_tick(bool enabled)
{
    common::MainThreadCallList::Unregister(stream_power_tick_task, NULL);
    if (enabled) {
        common::MainThreadCallList::Register(stream_power_tick_task, NULL);
    }
}

static void append_statistics_line(char *text, size_t size, size_t *used,
    const char *format, ...)
{
    if (*used >= size) {
        return;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(text + *used, size - *used, format, args);
    va_end(args);
    if (written > 0) {
        *used += (size_t)written < size - *used ? (size_t)written : size - *used;
    }
}

static void format_stream_statistics(const MoonlightStreamStats &stats,
    char *text, size_t size)
{
    size_t used = 0;
    const bool fresh = stats.active && stats.has_video && stats.video_age_us < 2000000ull;
    append_statistics_line(text, size, &used,
        "Video: %dx%d | H.264 | %d FPS | %.2f Mbps\n"
        "FPS: incoming %.1f | decode %.1f | PAF %.1f",
        stats.width, stats.height, stats.target_fps, stats.payload_mbps,
        stats.incoming_fps, stats.decoded_fps, stats.paf_submit_fps);
    if (stats.rtt_valid) {
        append_statistics_line(text, size, &used, "\nRTT: %u ms | variation: %u ms",
            stats.rtt_ms, stats.rtt_variation_ms);
    }
    if (fresh) {
        if (stats.incoming_fps > 0 && stats.decoded_fps > 0) {
            append_statistics_line(text, size, &used,
                "\nDrops: missing %.2f%% | pacer %.2f%% | PAF %.2f%%",
                stats.missing_percent, stats.pacer_drop_percent, stats.replaced_percent);
        }
        if (stats.decoder_call.samples || stats.paf_queue.samples) {
            append_statistics_line(text, size, &used, "\n");
            if (stats.decoder_call.samples) {
                append_statistics_line(text, size, &used, "Decode call: %.2f ms",
                    stats.decoder_call.average_ms);
            }
            if (stats.paf_queue.samples) {
                append_statistics_line(text, size, &used, "%sPAF queue: %.2f ms",
                    stats.decoder_call.samples ? " | " : "", stats.paf_queue.average_ms);
            }
        }
        if (stats.host_latency_samples) {
            append_statistics_line(text, size, &used, "\nHost min/max/avg: %.1f / %.1f / %.1f ms",
                stats.host_latency_min_ms, stats.host_latency_max_ms, stats.host_latency_average_ms);
        }
    } else {
        const char *status = !stats.active ? "Stream stopped" : !stats.has_video
            ? "Waiting for video" : "Video stalled";
        append_statistics_line(text, size, &used, "\n%s", status);
        if (stats.has_video && stats.active) {
            append_statistics_line(text, size, &used, " (%.0f s without frames)",
                (double)stats.video_age_us / 1000000.0);
        }
    }
}

static void send_keyboard_text(const SceWChar16 *text)
{
    unsigned int i;

    if (!text) {
        return;
    }
    for (i = 0; text[i] != 0 && i < 128; ++i) {
        if (text[i] >= 0x20 && text[i] <= 0x7e) {
            LiSendKeyboardEvent((short)text[i], KEY_ACTION_DOWN, 0);
            LiSendKeyboardEvent((short)text[i], KEY_ACTION_UP, 0);
        }
    }
}

static int ime_language(int layout)
{
    switch (layout) {
    case 1: return SCE_IME_LANGUAGE_GERMAN;
    case 2: return SCE_IME_LANGUAGE_SPANISH;
    case 3: return SCE_IME_LANGUAGE_FRENCH;
    case 4: return SCE_IME_LANGUAGE_RUSSIAN;
    default: return SCE_IME_LANGUAGE_ENGLISH;
    }
}

void Stream::OpenKeyboardIfRequested()
{
    static int module_loaded = 0;
    static int dialog_open = 0;
    static SceImeDialogParam param;
    static SceWChar16 buffer[128];
    static SceWChar16 title[] = { 'K', 'e', 'y', 'b', 'o', 'a', 'r', 'd', 0 };

    if (!dialog_open && vita_input_consume_keyboard_request()) {
        if (!module_loaded) {
            if (sceSysmoduleLoadModule(SCE_SYSMODULE_IME) < 0) {
                return;
            }
            module_loaded = 1;
        }
        memset(buffer, 0, sizeof(buffer));
        sceImeDialogParamInit(&param);
        param.supportedLanguages = ime_language(vita_input_keyboard_layout());
        param.languagesForced = SCE_TRUE;
        param.type = SCE_IME_TYPE_DEFAULT;
        param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
        param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
        param.title = title;
        param.maxTextLength = 120;
        param.inputTextBuffer = buffer;
        param.enterLabel = SCE_IME_ENTER_LABEL_GO;
        if (sceImeDialogInit(&param) >= 0) {
            dialog_open = 1;
        }
    }

    if (!dialog_open ||
        sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return;
    }

    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    sceImeDialogTerm();
    dialog_open = 0;
    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        send_keyboard_text(buffer);
    }
}

Stream::Stream()
    : Base("page_stream", NULL,
           Plugin::TransitionType_None,
           Plugin::TransitionType_None),
      m_video_plane(NULL),
      m_statistics_text(NULL),
      m_statistics_background(NULL),
      m_statistics_generation(0),
      m_statistics_next_update_us(0),
      m_video_plane_width(0),
      m_video_plane_height(0)
{
    unsigned int i;

    for (i = 0; i < kSurfaceSlotCount; ++i) {
        m_surface_slots[i].buffer = NULL;
        m_surface_slots[i].width = 0;
        m_surface_slots[i].height = 0;
        m_surface_slots[i].pitch = 0;
        m_surface_slots[i].surface.clear();
    }

    if (!root) {
        return;
    }
    m_video_plane = static_cast<ui::Plane *>(
        root->FindChild("plane_stream_video"));
    m_statistics_text = static_cast<ui::Text *>(root->FindChild("text_stream_statistics"));
    m_statistics_background = root->FindChild("plane_stream_statistics");
    if (m_statistics_text) {
        m_statistics_text->Hide(common::transition::Type_Reset);
    }
    if (m_statistics_background) {
        m_statistics_background->Hide(common::transition::Type_Reset);
    }

    vita_debug_log(
        "[StreamPage] root=%p video_plane=%p",
        root,
        m_video_plane);

    if (m_video_plane) {
        thread::RMutex::main_thread_mutex.Lock();
        s_instance = this;
        s_pending_frame.valid = false;
        thread::RMutex::main_thread_mutex.Unlock();
        paf::graph::PlaneObj *plane_obj =
            static_cast<paf::graph::PlaneObj *>(
                m_video_plane->GetDrawObj(paf::ui::Plane::OBJ_PLANE));
        vita_debug_log(
            "[StreamPage] video plane obj=%p",
            plane_obj);

        /* Keep the Vita awake only while the streaming page is active. */
        set_stream_power_tick(true);
        MoonlightApp::Instance()->HideStreamInfobar();
        common::MainThreadCallList::Register(PresentTask, this);
        MoonlightStreamStats stats;
        if (m_statistics_text && m_statistics_background &&
            MoonlightApp::Instance()->Connection().GetStreamStats(&stats) == 0 && stats.enabled) {
            m_statistics_generation = stats.generation;
            UpdateStatistics();
            common::MainThreadCallList::Register(StatisticsTask, this);
        }
    }
}

Stream::~Stream()
{
    common::MainThreadCallList::Unregister(PresentTask, this);
    common::MainThreadCallList::Unregister(StatisticsTask, this);
    set_stream_power_tick(false);
    thread::RMutex::main_thread_mutex.Lock();
    if (s_instance == this) {
        s_instance = NULL;
        s_pending_frame.valid = false;
    }
    thread::RMutex::main_thread_mutex.Unlock();
    /*
     * Stop PAF from sampling the external framebuffer before dropping the
     * Surface references. The decoder releases the framebuffer pool only
     * after this page has been destroyed.
     */
    if (m_video_plane) {
        /* Restore the system bar before Base reactivates the previous page. */
        MoonlightApp::Instance()->RestoreStreamInfobar();
        m_video_plane->Hide(common::transition::Type_Reset);
        m_video_plane->SetActivate(false);

        /*
         * Match NetStream's teardown order: detach the external texture from
         * the plane before dropping the Surface references. We cannot reuse
         * the external framebuffer after this point.
         */
        m_video_plane->SetTexture(paf::intrusive_ptr<paf::graph::Surface>());
    }

    unsigned int i;

    for (i = 0; i < kSurfaceSlotCount; ++i) {
        m_surface_slots[i].surface.clear();
        m_surface_slots[i].buffer = NULL;
        m_surface_slots[i].width = 0;
        m_surface_slots[i].height = 0;
        m_surface_slots[i].pitch = 0;
    }

    m_video_plane = NULL;
}

Stream *Stream::Instance()
{
    return s_instance;
}

void Stream::PresentTask(void *userdata)
{
    Stream *stream = static_cast<Stream *>(userdata);
    if (!stream || stream != s_instance) {
        return;
    }
    Frame frame = { NULL, 0, 0, 0, false, 0, 0 };

    thread::RMutex::main_thread_mutex.Lock();

    if (s_pending_frame.valid) {
        frame = s_pending_frame;
        s_pending_frame.valid = false;
    }

    thread::RMutex::main_thread_mutex.Unlock();

    stream->OpenKeyboardIfRequested();
    if (frame.valid) {
        stream->PresentFrame(frame);
    }
}

void Stream::ReleaseFrameBuffersTask(void *userdata)
{
    unsigned int generation =
        (unsigned int)(uintptr_t)userdata;

    /*
     * A delayed task from an older decoder session must never release the
     * framebuffer pool belonging to a newer session.
     */
    if (generation != moonlight_video_get_frame_pool_generation() ||
        s_instance != NULL) {
        common::MainThreadCallList::Unregister(ReleaseFrameBuffersTask, userdata);
        return;
    }

    if (s_release_delay != 0) {
        --s_release_delay;
        return;
    }

    common::MainThreadCallList::Unregister(ReleaseFrameBuffersTask, userdata);
    moonlight_video_release_frame_buffers();
}

void Stream::ScheduleFrameBufferRelease()
{
    unsigned int generation =
        moonlight_video_get_frame_pool_generation();

    s_release_delay = 3;
    common::MainThreadCallList::Unregister(
        ReleaseFrameBuffersTask, (void *)(uintptr_t)generation);
    common::MainThreadCallList::Register(
        ReleaseFrameBuffersTask,
        (void *)(uintptr_t)generation);
}

Stream::SurfaceSlot *Stream::FindSurfaceSlot(void *buffer)
{
    unsigned int i;
    SurfaceSlot *free_slot = NULL;

    if (!buffer) {
        return NULL;
    }

    for (i = 0; i < kSurfaceSlotCount; ++i) {
        if (m_surface_slots[i].buffer == buffer) {
            return &m_surface_slots[i];
        }
        if (!free_slot && m_surface_slots[i].buffer == NULL) {
            free_slot = &m_surface_slots[i];
        }
    }

    return free_slot;
}

void Stream::PresentFrame(const Frame &frame)
{
    const uint64_t start_us = frame.stats_generation ? LiGetMicroseconds() : 0;
    if (!m_video_plane || !frame.buffer ||
        frame.width == 0 || frame.height == 0 || frame.pitch == 0) {
        return;
    }

    SurfaceSlot *slot = FindSurfaceSlot(frame.buffer);
    if (!slot) {
        return;
    }

    if (slot->buffer != frame.buffer ||
        slot->width != frame.width ||
        slot->height != frame.height ||
        slot->pitch != frame.pitch) {
        slot->surface.clear();
        slot->buffer = frame.buffer;
        slot->width = frame.width;
        slot->height = frame.height;
        slot->pitch = frame.pitch;
    }

    if (!slot->surface.get()) {
        slot->surface = new graph::Surface(
            frame.width,
            frame.height,
            ImageMode_RGBA8888,
            ImageOrder_Linear,
            1,
            frame.buffer,
            frame.pitch * 4,
            1,
            0);
    }

    if (slot->surface.get()) {
        m_video_plane->SetTexture(slot->surface);

        const bool center_region = moonlight_video_center_region() != 0;
        const unsigned int plane_width = center_region ? frame.width : 960;
        const unsigned int plane_height = center_region ? frame.height : 544;

        if (m_video_plane_width != plane_width ||
            m_video_plane_height != plane_height) {
            /*
             * Fit mode keeps a full-screen plane so its background fills the
             * letterbox area. Center-region mode scales a larger aspect-correct
             * surface beyond the screen and relies on the page viewport to
             * clip the outer edges.
             */
            m_video_plane->SetSize(paf::math::v4(
                (float)plane_width,
                (float)plane_height,
                0.0f,
                0.0f));
            /*
             * PAF uses a center-origin coordinate system: (0, 0) centers
             * the plane on the screen. Keep oversized crop surfaces at the
             * origin so the viewport clips equally on both sides.
             */
            m_video_plane->SetPos(paf::math::v4(0.0f, 0.0f, 0.0f));
            m_video_plane_width = plane_width;
            m_video_plane_height = plane_height;

            paf::graph::PlaneObj *plane_obj =
                static_cast<paf::graph::PlaneObj *>(
                    m_video_plane->GetDrawObj(paf::ui::Plane::OBJ_PLANE));
            if (plane_obj) {
                if (center_region) {
                    plane_obj->SetScaleMode(
                        paf::graph::PlaneObj::SCALE_SIZE,
                        paf::graph::PlaneObj::SCALE_SIZE);
                } else {
                    plane_obj->SetScaleMode(
                        paf::graph::PlaneObj::SCALE_ASPECT_SIZE,
                        paf::graph::PlaneObj::SCALE_ASPECT_SIZE);
                }
            }
        }
    }

    if (slot->surface.get() && frame.stats_generation) {
        moonlight_video_note_submitted(frame.stats_generation,
            start_us - frame.queued_us, LiGetMicroseconds() - start_us);
    }

    OpenKeyboardIfRequested();
}

void Stream::StatisticsTask(void *userdata)
{
    Stream *stream = static_cast<Stream *>(userdata);
    if (stream == s_instance && LiGetMicroseconds() >= stream->m_statistics_next_update_us) {
        stream->UpdateStatistics();
    }
}

void Stream::UpdateStatistics()
{
    MoonlightStreamStats stats;
    m_statistics_next_update_us = LiGetMicroseconds() + 1000000ull;
    if (MoonlightApp::Instance()->Connection().GetStreamStats(&stats) != 0 ||
        !stats.enabled || stats.generation != m_statistics_generation) {
        m_statistics_text->Hide(common::transition::Type_Reset);
        m_statistics_background->Hide(common::transition::Type_Reset);
        return;
    }
    char text[768];
    format_stream_statistics(stats, text, sizeof(text));
    m_statistics_text->SetString(common::string_util::ToWString(text));
    m_statistics_background->Show(common::transition::Type_Reset);
    m_statistics_text->Show(common::transition::Type_Reset);
}

void Stream::QueueFrame(
    void *buffer,
    unsigned int width,
    unsigned int height,
    unsigned int pitch,
    uint32_t stats_generation)
{
    const uint64_t queued_us = stats_generation ? LiGetMicroseconds() : 0;

    thread::RMutex::main_thread_mutex.Lock();

    if (s_instance != NULL && buffer != NULL) {
        const bool replaced = s_pending_frame.valid &&
            s_pending_frame.stats_generation == stats_generation;
        s_pending_frame.buffer = buffer;
        s_pending_frame.width = width;
        s_pending_frame.height = height;
        s_pending_frame.pitch = pitch;
        s_pending_frame.stats_generation = stats_generation;
        s_pending_frame.queued_us = queued_us;
        s_pending_frame.valid = true;
        moonlight_video_note_queued(stats_generation, replaced);
    }

    thread::RMutex::main_thread_mutex.Unlock();
}

void Stream::InvalidateFrame()
{
    thread::RMutex::main_thread_mutex.Lock();
    s_pending_frame.valid = false;
    thread::RMutex::main_thread_mutex.Unlock();
}

extern "C" void moonlight_video_present(
    void *buffer,
    unsigned int width,
    unsigned int height,
    unsigned int pitch,
    uint32_t stats_generation)
{
    Stream::QueueFrame(buffer, width, height, pitch, stats_generation);
}

extern "C" void moonlight_video_invalidate(void)
{
    Stream::InvalidateFrame();
}

}
