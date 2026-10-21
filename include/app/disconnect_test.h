#ifndef VITA_MOONLIGHT_DISCONNECT_TEST_H
#define VITA_MOONLIGHT_DISCONNECT_TEST_H

#include "moonlight/types.h"

class MoonlightApp;

/* Test build only: CMake selects one implementation under the shared hook guard.
 * Hooks run on the PAF main thread except the explicitly marked worker checkpoints.
 * No worker/UI callback replacement. */
void disconnect_test_capture_host(const MoonlightHost &host);
void disconnect_test_poll(MoonlightApp &app);
void disconnect_test_event(MoonlightApp &app, const MoonlightEvent &event);
void disconnect_test_shutdown();

#if defined(MOONLIGHT_PENDING_DISCONNECT_TEST) || defined(MOONLIGHT_STARTUP_DISCONNECT_TEST)
void disconnect_test_capture_start(int application_id, int result);
#endif

#ifdef MOONLIGHT_PENDING_DISCONNECT_TEST
/* Stream worker only; test-only bounded checkpoint, no PAF access. */
extern "C" void disconnect_test_pending_checkpoint(int application_id);
#endif

#ifdef MOONLIGHT_STARTUP_DISCONNECT_TEST
/* Sole stream worker, inside LiStartConnection stage callback; no PAF access. */
extern "C" void disconnect_test_startup_checkpoint(int stage);
#endif

#endif
