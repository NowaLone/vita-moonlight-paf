#ifndef VITA_MOONLIGHT_DISCONNECT_TEST_H
#define VITA_MOONLIGHT_DISCONNECT_TEST_H

#include "moonlight/types.h"

class MoonlightApp;

/* Test build only: CMake selects one idle/active implementation under the shared hook guard.
 * All hooks run on the PAF main thread; no worker/UI callback replacement. */
void disconnect_test_capture_host(const MoonlightHost &host);
void disconnect_test_poll(MoonlightApp &app);
void disconnect_test_event(MoonlightApp &app, const MoonlightEvent &event);
void disconnect_test_shutdown();

#endif
