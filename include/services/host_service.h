#ifndef VITA_MOONLIGHT_HOST_SERVICE_H
#define VITA_MOONLIGHT_HOST_SERVICE_H

#include <stdint.h>
#include "moonlight/api.h"

class HostService {
public:
    int GetHosts(MoonlightHost *out, int capacity);
    int Search();
    int Add(const char *address, uint16_t port, const char *name);
    int Pair(const char *address);
    int StartStream(const char *address);
    int StopStream();
};

#endif
