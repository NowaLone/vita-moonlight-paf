#ifndef VITA_MOONLIGHT_PAGE_STREAM_H
#define VITA_MOONLIGHT_PAGE_STREAM_H

#include "pages/page.h"
#include "moonlight/controller_light.h"
#include "moonlight/stream_stats.h"
#include <stdint.h>

namespace page {

class Stream : public Base {
public:
    Stream();
    virtual ~Stream();

    static void QueueFrame(
        void *buffer,
        unsigned int width,
        unsigned int height,
        unsigned int pitch,
        uint32_t stats_generation);
    static void InvalidateFrame();
    static void PresentTask(void *userdata);
    static void ReleaseFrameBuffersTask(void *userdata);
    static void ScheduleFrameBufferRelease();

private:
    static const unsigned int kSurfaceSlotCount = 3;

    struct Frame {
        void *buffer;
        unsigned int width;
        unsigned int height;
        unsigned int pitch;
        bool valid;
        uint32_t stats_generation;
        uint64_t queued_us;
    };

    struct SurfaceSlot {
        void *buffer;
        unsigned int width;
        unsigned int height;
        unsigned int pitch;
        paf::intrusive_ptr<paf::graph::Surface> surface;
    };

    void PresentFrame(const Frame &frame);
    static void StatisticsTask(void *userdata);
    void UpdateStatistics();
    void UpdateWarning(const MoonlightStreamStats &stats, uint64_t now_us);
    void UpdateControllerLight();
    void OpenKeyboardIfRequested();
    SurfaceSlot *FindSurfaceSlot(void *buffer);

    paf::ui::Plane *m_video_plane;
    paf::ui::Text *m_statistics_text;
    paf::ui::Widget *m_statistics_background;
    paf::ui::Text *m_warning_text;
    paf::ui::Widget *m_warning_background;
    MoonlightStreamWarning m_warning;
    uint64_t m_warning_until_us;
    paf::ui::Widget *m_light_ds4;
    paf::ui::Widget *m_light_left;
    paf::ui::Widget *m_light_right;
    MoonlightControllerLight m_controller_light;
    uint32_t m_controller_light_generation;
    uint32_t m_statistics_generation;
    uint64_t m_statistics_next_update_us;
    uint64_t m_statistics_next_poll_us;
    unsigned int m_video_plane_width;
    unsigned int m_video_plane_height;
    SurfaceSlot m_surface_slots[kSurfaceSlotCount];

    static Stream *s_instance;
    static Frame s_pending_frame;
    static unsigned int s_release_delay;
};

}

#endif
