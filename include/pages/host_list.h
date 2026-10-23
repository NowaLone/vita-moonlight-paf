#ifndef VITA_MOONLIGHT_HOST_LIST_H
#define VITA_MOONLIGHT_HOST_LIST_H

#include <paf.h>
#include <string.h>
#include "moonlight/types.h"

namespace page {

/* Both stores support a count query. Discovery returns total available, while
 * the saved-host store returns the number copied. Retry discovery growth before
 * publishing the snapshot; its existing backend limit is unchanged. */
template<class Service>
bool read_host_snapshot(Service &service, paf::vector<MoonlightHost> &hosts)
{
    int capacity = service.GetHosts(NULL, 0);
    if (capacity < 0) return false;
    hosts.resize(capacity);
    while (capacity > 0) {
        int copied = service.GetHosts(hosts.data(), capacity);
        if (copied < 0) return false;
        if (copied <= capacity) {
            hosts.resize(copied);
            return true;
        }
        capacity = copied;
        hosts.resize(capacity);
    }
    return true;
}

/* Discovery never supplies a UUID/MAC today. Use them only when actually present,
 * then compare usable routes. A display name is never discovery identity. */
inline bool same_discovered_host(const MoonlightHost &left, const MoonlightHost &right)
{
    if (left.host_id[0] && right.host_id[0])
        return strcmp(left.host_id, right.host_id) == 0;
    if (left.mac[0] && right.mac[0])
        return strcmp(left.mac, right.mac) == 0;
    if (left.port != right.port) return false;
    return (left.internal[0] && right.internal[0] &&
            strcmp(left.internal, right.internal) == 0) ||
           (left.external[0] && right.internal[0] &&
            strcmp(left.external, right.internal) == 0) ||
           (left.internal[0] && right.external[0] &&
            strcmp(left.internal, right.external) == 0);
}

inline int canonical_host_index(const MoonlightHost *hosts, int count,
                                const MoonlightHost &selected)
{
    for (int i = 0; i < count; ++i) {
        if (hosts[i].port == selected.port &&
            strcmp(hosts[i].internal, selected.internal) == 0) return i;
    }
    return -1;
}

/* One native cell edit at a time. Retained tokens identify the existing widgets.
 * Prefer removing obsolete cells, then moving/inserting toward the desired order.
 * ponytail: repeated linear scans suit short PC lists; index tokens if lists grow large. */
inline int next_host_cell_edit(const uint32_t *current, int count,
                               const uint32_t *desired, int wanted, int *from, int *to)
{
    for (int i = count - 1; i >= 0; --i) {
        bool keep = false;
        for (int j = 0; j < wanted; ++j)
            if (current[i] == desired[j]) { keep = true; break; }
        if (!keep) { *from = i; return 1; }
    }
    for (int i = 0; i < wanted; ++i) {
        if (i < count && current[i] == desired[i]) continue;
        *to = i;
        for (int j = i + 1; j < count; ++j)
            if (current[j] == desired[i]) { *from = j; return 2; }
        return 3;
    }
    return 0;
}

}

#endif
