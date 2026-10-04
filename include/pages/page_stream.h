#ifndef VITA_MOONLIGHT_PAGE_STREAM_H
#define VITA_MOONLIGHT_PAGE_STREAM_H

#include "pages/page.h"

namespace page {

class Stream : public Base {
public:
    Stream();
    virtual ~Stream();
    virtual Type GetType() { return Type_Stream; }

    static Stream *Instance();
    static void QueueFrame(
        void *buffer,
        unsigned int width,
        unsigned int height,
        unsigned int pitch);
    static void InvalidateFrame();
    static void PresentTask(void *userdata);
    static void ReleaseFrameBuffersTask(void *userdata);
    static void ScheduleFrameBufferRelease();

private:
    class PadListener : public paf::inputdevice::InputListener {
    public:
        explicit PadListener(Stream *parent)
            : InputListener(paf::inputdevice::DEVICE_TYPE_PAD),
              m_parent(parent) {}

        virtual void OnUpdate(paf::inputdevice::Data *data) {
            if (m_parent) {
                m_parent->OnPadUpdate(data);
            }
        }

    private:
        Stream *m_parent;
    };

    static const unsigned int kSurfaceSlotCount = 3;

    struct Frame {
        void *buffer;
        unsigned int width;
        unsigned int height;
        unsigned int pitch;
        bool valid;
    };

    struct SurfaceSlot {
        void *buffer;
        unsigned int width;
        unsigned int height;
        unsigned int pitch;
        paf::intrusive_ptr<paf::graph::Surface> surface;
    };

    void PresentFrame(const Frame &frame);
    SurfaceSlot *FindSurfaceSlot(void *buffer);
    void OnPadUpdate(paf::inputdevice::Data *data);

    paf::ui::Plane *m_video_plane;
    SurfaceSlot m_surface_slots[kSurfaceSlotCount];
    paf::common::SharedPtr<paf::inputdevice::InputListener> m_pad_listener;
    bool m_stopping;

    static Stream *s_instance;
    static Frame s_pending_frame;
    static bool s_task_registered;
    static unsigned int s_release_delay;
};

}

#endif
