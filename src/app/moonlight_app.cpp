#include "app/moonlight_app.h"
#include "moonlight/api.h"

namespace {
MoonlightApp s_app;
}

MoonlightApp::MoonlightApp()
    : m_initialized(false)
{
}

MoonlightApp::~MoonlightApp()
{
}

MoonlightApp *MoonlightApp::Instance()
{
    return &s_app;
}

int MoonlightApp::Initialize()
{
    if (m_initialized) {
        return 0;
    }

    int result = moonlight_api_init();
    if (result != 0) {
        return result;
    }

    m_initialized = true;
    return 0;
}

void MoonlightApp::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    moonlight_api_shutdown();
    m_initialized = false;
}
