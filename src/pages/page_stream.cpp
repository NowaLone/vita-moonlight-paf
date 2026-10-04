#include <paf.h>

#include "pages/page_stream.h"

using namespace paf;

namespace page {

Stream *Stream::s_instance = NULL;
Stream::Frame Stream::s_pending_frame = { NULL, 0, 0, 0, false };
bool Stream::s_task_registered = false;

Stream::Stream()
    : Base("page_stream", NULL,
           Plugin::TransitionType_None,
           Plugin::TransitionType_None),
      m_video_plane(NULL),
      m_surface()
{
    s_instance = this;

    if (!root) {
        return;
    }

    m_video_plane = static_cast<ui::Plane *>(
        root->FindChild("plane_stream_video"));
}

Stream::~Stream()
{
    if (s_instance == this) {
        s_instance = NULL;
    }

    m_surface.reset();
    m_video_plane = NULL;
}

Stream *Stream::Instance()
{
    return s_instance;
}

void Stream::PresentTask(void *)
{
    while (true) {
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
    }
}

void Stream::PresentFrame(const Frame &frame)
{
    if (!m_video_plane || !frame.buffer ||
        frame.width == 0 || frame.height == 0 || frame.pitch == 0) {
        return;
    }

    if (!m_surface.get()) {
        m_surface = new graph::Surface(
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

    if (m_surface.get()) {
        m_video_plane->SetTexture(m_surface);
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
