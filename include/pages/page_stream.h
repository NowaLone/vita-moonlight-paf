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

private:
    struct Frame {
        void *buffer;
        unsigned int width;
        unsigned int height;
        unsigned int pitch;
        bool valid;
    };

    void PresentFrame(const Frame &frame);

    paf::ui::Plane *m_video_plane;
    paf::intrusive_ptr<paf::graph::Surface> m_surface;

    static Stream *s_instance;
    static Frame s_pending_frame;
    static bool s_task_registered;
};

}

#endif
