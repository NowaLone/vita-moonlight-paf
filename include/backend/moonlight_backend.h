#ifndef VITA_MOONLIGHT_BACKEND_H
#define VITA_MOONLIGHT_BACKEND_H

#include "moonlight/api.h"

class MoonlightBackend {
public:
    virtual ~MoonlightBackend() {}

    virtual int Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual int OpenSettings() = 0;
    virtual int GetSettings(MoonlightSettings *out) = 0;
    virtual int GetSettingValue(MoonlightSettingKey key, int *out_value) = 0;
    virtual int SetSettingValue(MoonlightSettingKey key, int value) = 0;

    virtual int SetEventCallback(MoonlightEventCallback callback, void *userdata) = 0;

    virtual int GetHosts(MoonlightHost *out, int capacity) = 0;
    virtual int SearchHosts() = 0;
    virtual int AddHost(const char *address, uint16_t port, const char *name) = 0;

    virtual int PairHost(const char *address) = 0;

    virtual int StartStream(const char *address) = 0;
    virtual int StopStream() = 0;
};

#endif
