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

/* A conflicting stronger identity must not inherit another PC's selection. */
inline bool same_host_identity(const MoonlightHost &left, const MoonlightHost &right)
{
    if (left.host_id[0] && right.host_id[0]) {
        return strcmp(left.host_id, right.host_id) == 0;
    }
    if (left.mac[0] && right.mac[0]) {
        return strcmp(left.mac, right.mac) == 0;
    }
    if (left.internal[0] && right.internal[0]) {
        return left.port == right.port && strcmp(left.internal, right.internal) == 0;
    }
    if (left.external[0] && right.external[0]) {
        return left.port == right.port && strcmp(left.external, right.external) == 0;
    }
    return !left.internal[0] && !left.external[0] &&
        !right.internal[0] && !right.external[0] && left.name[0] &&
        strcmp(left.name, right.name) == 0;
}

}
#endif
