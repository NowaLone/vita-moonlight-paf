#ifndef VITA_MOONLIGHT_PAGE_HOST_AVAILABILITY_H
#define VITA_MOONLIGHT_PAGE_HOST_AVAILABILITY_H

#include "app/moonlight_app.h"
#include "common.h"

namespace page {
inline void set_host_availability(paf::ui::Widget *item, const MoonlightHost &host)
{
    if (!item) return;
    paf::ui::Widget *dot = item->FindChild("plane_host_availability");
    if (!dot) dot = item->FindChild("plane_host_select_availability");
    if (dot) {
        static_cast<paf::ui::Plane *>(dot)->SetTexture(g_plugin->GetTexture(
            MoonlightApp::Instance()->Hosts().IsAvailable(host)
                ? "tex_host_available" : "tex_host_unconfirmed"));
    }
}

/* Called only by MoonlightApp main-thread event delivery; workers publish observations only.
 * Keep generated cells, focus, scroll and selection intact. Future cells use the factory. */
inline void update_host_availability(paf::ui::ListView *list,
                                     const paf::vector<MoonlightHost> &hosts)
{
    if (!list) return;
    for (int i = 0; i < (int)hosts.size(); ++i) {
        set_host_availability(list->GetListItem(0, i), hosts[i]);
    }
}
}
#endif
