#include <paf.h>
#include <stdint.h>

#include "debug.h"
#include "app/moonlight_app.h"
#include "pages/page_stream.h"
#include "../moonlight/vita_video_renderer.h"

using namespace paf;

namespace page {

Stream *Stream::s_instance = NULL;
Stream::Frame Stream::s_pending_frame = { NULL, 0, 0, 0, false };
bool Stream::s_task_registered = false;
unsigned int Stream::s_release_delay = 0;

Stream::Stream()
    : Base("page_stream", NULL,
           Plugin::TransitionType_None,
           Plugin::TransitionType_None),
      m_video_plane(NULL),
      m_stopping(false)
{
    unsigned int i;

    s_instance = this;

    for (i = 0; i < kSurfaceSlotCount; ++i) {
        m_surface_slots[i].buffer = NULL;
        m_surface_slots[i].width = 0;
        m_surface_slots[i].height = 0;
        m_surface_slots[i].pitch = 0;
        m_surface_slots[i].surface.clear();
    }

    if (!root) {
        if (s_instance == this) {
            s_instance = NULL;
        }
        return;
    }

    paf::common::SharedPtr<paf::inputdevice::InputListener> listener(
        new PadListener(this));
    m_pad_listener = listener;
    paf::inputdevice::AddInputListener(m_pad_listener);

    m_video_plane = static_cast<ui::Plane *>(
        root->FindChild("plane_stream_video"));

    vita_debug_log(
        "[StreamPage] root=%p video_plane=%p",
        root,
        m_video_plane);

    if (m_video_plane) {
        paf::graph::PlaneObj *plane_obj =
            static_cast<paf::graph::PlaneObj *>(
                m_video_plane->GetDrawObj(paf::ui::Plane::OBJ_PLANE));
        vita_debug_log(
            "[StreamPage] video plane obj=%p",
            plane_obj);
    }
}

Stream::~Stream()
{
    if (s_instance == this) {
        s_instance = NULL;
    }

    paf::inputdevice::DelInputListener(m_pad_listener);

    /*
     * Stop PAF from sampling the external framebuffer before dropping the
     * Surface references. The decoder releases the framebuffer pool only
     * after this page has been destroyed.
     */
    if (m_video_plane) {
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

void Stream::PresentTask(void *)
{
    Frame frame = { NULL, 0, 0, 0, false };

    thread::RMutex::main_thread_mutex.Lock();

    if (!s_pending_frame.valid) {
        s_task_registered = false;
        thread::RMutex::main_thread_mutex.Unlock();
        return;
    }

    frame = s_pending_frame;
    s_pending_frame.valid = false;

    thread::RMutex::main_thread_mutex.Unlock();

    Stream *stream = s_instance;
    if (stream) {
        stream->PresentFrame(frame);
    }

    /*
     * MainThreadCallList callbacks must return promptly. Keep at most one
     * callback queued while the decoder continuously produces frames.
     */
    bool register_task = false;
    thread::RMutex::main_thread_mutex.Lock();

    if (s_pending_frame.valid && s_instance != NULL) {
        register_task = true;
    } else {
        s_task_registered = false;
    }

    thread::RMutex::main_thread_mutex.Unlock();

    if (register_task) {
        common::MainThreadCallList::Register(PresentTask, NULL);
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
        return;
    }

    if (s_release_delay != 0) {
        --s_release_delay;
        common::MainThreadCallList::Register(
            ReleaseFrameBuffersTask,
            userdata);
        return;
    }

    moonlight_video_release_frame_buffers();
}

void Stream::ScheduleFrameBufferRelease()
{
    unsigned int generation =
        moonlight_video_get_frame_pool_generation();

    s_release_delay = 3;
    common::MainThreadCallList::Register(
        ReleaseFrameBuffersTask,
        (void *)(uintptr_t)generation);
}

void Stream::OnPadUpdate(paf::inputdevice::Data *data)
{
    if (!data || !data->m_pad_data || !data->m_pad_data_pre || m_stopping) {
        return;
    }

    const uint32_t buttons = data->m_pad_data->paddata;
    const uint32_t previous = data->m_pad_data_pre->paddata;

    if ((buttons & paf::inputdevice::pad::Data::PAD_ESCAPE) &&
        !(previous & paf::inputdevice::pad::Data::PAD_ESCAPE)) {
        m_stopping = true;
        vita_debug_log("[StreamPage] stopping stream from PAD_ESCAPE");
        MoonlightApp::Instance()->Connection().Stop();
    }
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

        paf::graph::PlaneObj *plane_obj =
            static_cast<paf::graph::PlaneObj *>(
                m_video_plane->GetDrawObj(paf::ui::Plane::OBJ_PLANE));
        if (plane_obj) {
            plane_obj->SetScaleMode(
                paf::graph::PlaneObj::SCALE_ASPECT_SIZE,
                paf::graph::PlaneObj::SCALE_ASPECT_SIZE);
        }
    }
}

void Stream::QueueFrame(
    void *buffer,
    unsigned int width,
    unsigned int height,
    unsigned int pitch)
{
    bool register_task = false;

    thread::RMutex::main_thread_mutex.Lock();

    if (s_instance != NULL && buffer != NULL) {
        s_pending_frame.buffer = buffer;
        s_pending_frame.width = width;
        s_pending_frame.height = height;
        s_pending_frame.pitch = pitch;
        s_pending_frame.valid = true;

        if (!s_task_registered) {
            s_task_registered = true;
            register_task = true;
        }
    }

    thread::RMutex::main_thread_mutex.Unlock();

    if (register_task) {
        common::MainThreadCallList::Register(
            PresentTask,
            NULL);
    }
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
    unsigned int pitch)
{
    Stream::QueueFrame(buffer, width, height, pitch);
}

extern "C" void moonlight_video_invalidate(void)
{
    Stream::InvalidateFrame();
}

}
