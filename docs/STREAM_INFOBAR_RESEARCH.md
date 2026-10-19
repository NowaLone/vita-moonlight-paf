# System infobar during streaming

Research date: 2026-10-19. Branch: `codex/research-stream-infobar`.
Moonlight source baseline: `9ce5dcad89737a0e4bdac5bd8a7e44691554803a`.

**Result [code]:** use `sceAppMgrSetInfobarState()` to hide the Vita system status
bar while the Stream page is active and show it again on exit. NetStream explicitly
calls this API when its playback controls change visibility; video decoding does
not automatically control the bar. The follow-up implementation on this branch
adds these requests to Moonlight's page/application lifecycle. No build or hardware
validation was performed.

## Evidence and API

The primary reference is the clean local checkout at
`/home/nowaru/Desktop/NetStream`, commit
`f812de87f739a740ae71fa1c23cd5c788084530c` (2025-10-11). Source links below are pinned
to that revision. The web reader could not retrieve the NetStream pages, so those
findings were checked against the local Git files, not a fetched web copy.

In NetStream's [PlayerSimple implementation](https://github.com/GrapheneCt/NetStream/blob/f812de87f739a740ae71fa1c23cd5c788084530c/NetStream/source/menus/menu_player_simple.cpp):

- `OnVideoPlaneTouch()`, lines 33–59: hiding playback controls also requests an
  invisible infobar; revealing controls requests a visible infobar and records
  the time. The video control trigger is bound to this handler and `PAD_MENU`
  in `OnGenericPlayerStateChange()`, lines 152–160.
- `OnUpdate()`, lines 304–322: after 5,000,000 microseconds with controls shown,
  controls and infobar are hidden. Seeking, pause or a scale other than 1.0
  refresh the timestamp instead. This is an application timer, not an AppMgr
  automatic-hide feature.
- Constructor, line 618: requests invisible at player entry.
- Destructor, line 632: requests visible before returning to the previous menu.
- `SetScale()`, lines 779–800: full-screen scale 1.0 requests invisible; a reduced
  player requests visible. This additional mini-player behavior is separate from
  the controls timer and is not needed by Moonlight's current Stream page.

All these calls use `COLOR_BLACK` and `TRANSPARENCY_TRANSLUCENT`. NetStream does
not check their return values. Its parent server menu deletes the player on Back
and initialization failure ([OnPlayerEvent](https://github.com/GrapheneCt/NetStream/blob/f812de87f739a740ae71fa1c23cd5c788084530c/NetStream/source/menus/menu_server.cpp#L16-L32)),
thereby reaching the destructor's restore call.

The installed `/usr/local/vitasdk/arm-vita-eabi/include/psp2/appmgr.h`, lines 36–49
and 307–317, declares the enums and function. The
[current VitaSDK header](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/appmgr.h)
was also checked online: the same signature is present, with 0 for success and a
negative error result. The symbolic calls are:

```cpp
#include <psp2/appmgr.h>

// Enter full-screen playback.
int result = sceAppMgrSetInfobarState(
    SCE_APPMGR_INFOBAR_VISIBILITY_INVISIBLE,
    SCE_APPMGR_INFOBAR_COLOR_BLACK,
    SCE_APPMGR_INFOBAR_TRANSPARENCY_TRANSLUCENT);

// Leave playback, or reveal local playback controls.
result = sceAppMgrSetInfobarState(
    SCE_APPMGR_INFOBAR_VISIBILITY_VISIBLE,
    SCE_APPMGR_INFOBAR_COLOR_BLACK,
    SCE_APPMGR_INFOBAR_TRANSPARENCY_TRANSLUCENT);
```

This is a system infobar, not a PAF widget. No matching getter was found in the
reviewed AppMgr header: restoring visible/black/translucent is an explicit policy
matching NetStream, not restoration of a queried previous state. Verify its visual
match to Moonlight's browsing baseline on hardware **[open]**.

## Moonlight integration points at the research baseline [code]

- `CMakeLists.txt` already links `SceAppMgr_stub`. No additional import library
  is needed for this API; a new adapter source would still need to be listed.
- `src/app/paf_runtime.cpp` uses `graphics_option = 7`, as does NetStream's
  `source/main.cpp`. Keep it: it is global framework setup, whereas NetStream
  switches infobar visibility during playback through AppMgr.
- `src/pages/page.cpp` opens `page_stream` with `graphics_flag = 0x80` and draw
  priority 8. These existing render settings are separate from the explicit
  AppMgr calls found in NetStream; they are not a replacement for them.
- `cxml/vita_moonlight_ui.xml` already defines full-screen 960 × 544 background
  and video planes. There is no local playback-controls panel or infobar widget.
  `text_stream_fps` is a separate application overlay.
- `Apps::OnMoonlightEvent()` in `src/pages/page_apps.cpp`, lines 226–258,
  creates Stream on `STREAM_STARTED` and deletes it on `STREAM_STOPPED`.
  `STREAM_FAILED` only updates Apps' status; it does not delete a Stream page.
- `Stream::Stream()` in `src/pages/page_stream.cpp` returns early if `root` is
  missing. Its `m_video_plane` check is the existing entry point for stream-only
  power management; infobar ownership should begin only after these checks.
  `Stream::~Stream()` is the corresponding normal restore point.
- `MoonlightApp::ProcessEventTask()` delivers backend events on the PAF main
  thread. Use this existing page lifecycle, not protocol/decoder callbacks, to
  change infobar state.
- `MoonlightApp::Shutdown()` clears event delivery and shuts down the backend,
  but does not explicitly destroy the page stack. It therefore cannot rely on a
  final `STREAM_STOPPED` event or `Stream::~Stream()` to restore visibility.

## Implementation (2026-10-19) [code]

`MoonlightApp` owns `m_infobar_restore_pending` and exposes `HideStreamInfobar()` /
`RestoreStreamInfobar()` for the PAF main thread. A private AppMgr wrapper in the
existing application source performs the requests and logs visibility/result once
per call through `vita_debug_log()` (`[Infobar]`). No extra source/import library is
needed. The pending flag records the need to restore, not a successful hide.

Stream requests hidden only inside the valid `m_video_plane` constructor branch;
the corresponding destructor branch restores visible before Base reactivates Apps.
An invalid root or missing plane does not acquire infobar ownership. A failed hide
does not interrupt playback and is still followed by a restore attempt. A successful
restore clears pending state; a failed restore leaves it pending for the next cleanup
request. A new stream always makes a fresh hide request, including after failed cleanup.

`MoonlightApp::Shutdown()` restores before its initialization guard and before
disabling backend event delivery. This does not depend on a final stop callback
and permits another restore attempt on repeated shutdown if a previous attempt
failed. No frame task or input worker calls AppMgr to set visibility.

The stream-session review confirms manual stop and host termination emit
`STREAM_STOPPED` after a started stream; Apps deletes Stream on that event.
Startup failure without a Stream page makes no infobar request. No stream-event
semantics or framebuffer teardown ordering were changed.

The original integration plan follows for context; the adapter is implemented in
the existing `MoonlightApp` files rather than a separate class/source file.

## Integration plan

1. Isolate the AppMgr calls and error logging in a small application-layer
   adapter. Expose stream entry/exit and an idempotent restore operation, all
   called on the PAF main thread. Keep the streaming protocol/input/renderers
   independent of this presentation policy.
2. After Stream has a valid root and video plane, acquire its infobar scope and
   request invisible. Record ownership separately from API success so even a
   failed hide request can be followed by a best-effort visible request on exit.
   An invalid page that never acquired ownership must not change global state.
3. Release that scope in `Stream::~Stream()`, requesting visible before Apps is
   reactivated by the Base destructor. Normal stop and host termination already
   converge on `STREAM_STOPPED`. Failure before page creation leaves browsing
   visibility alone; review any failure arriving after page creation so it cannot
   strand the scope. Preserve existing framebuffer-release ordering.
4. Explicitly restore any owned infobar state in `MoonlightApp::Shutdown()` on
   the main thread, independently of backend stop-event delivery. Repeated restore
   requests must be harmless. This covers orderly shutdown, not a process crash.
5. Check every return value; send transition/error diagnostics only through
   `vita_debug_log()`. An infobar error should not abort a working stream. Do not
   mark a failed request as successfully applied or retry/log on every frame.

The implementation changes no CXML, settings, decode, touch or PS Button
handling. In particular, do not copy NetStream's tap-anywhere trigger: Moonlight
already sends touchscreen actions to the PC. If local stream controls are added
later, their actual shown/hidden state can drive visible/invisible calls; the
5-second NetStream timer is a reference, not a requirement for the current app.

Quick Actions is system UI, distinct from a local controls panel. Keep the existing
`isSystemUiOverlaid` input suppression and PS hold/tap behavior. Do not continually
force invisible while the system overlay is open. Whether Quick Actions, LiveArea
return or the native stream IME resets this infobar request remains **[open]**;
add a main-thread reapply on a confirmed return transition only if a device test
shows it is needed. `SceCommonDialogInfobarParam` configures common dialogs and is
not a substitute for the normal playback API investigated here.

## User verification (2026-10-19)

After implementation commit `85899c5`, the user confirmed that everything works.
This records successful user-reported Vita verification of the requested infobar
hiding/restoration. No individual failure/shutdown-case results, AppMgr return
logs or firmware/build details were supplied. It is not an agent-run build/test
or verification across firmware versions.

## Full maintainer validation matrix

The general confirmation above does not establish individual results for every
case below. Detailed lifecycle/failure coverage remains **[open]**:

1. Record the browsing bar appearance. Start a stream: bar disappears, video
   remains full-screen, FPS overlay follows its own setting, touch still reaches
   the host, and video/audio/controller behavior is unchanged.
2. Stop with double PS: Apps regains the bar; reconnect and repeat several times.
3. Fail launch before `STREAM_STARTED`: Apps retains the bar. Disconnect the
   host/network during an active stream: page cleanup restores the bar.
4. Hold PS for Quick Actions, close it, and return through LiveArea/suspend if
   supported: system UI works and playback resumes with the intended visibility;
   no stale host PS event occurs. Open/close the stream keyboard as well.
5. Exit the application while streaming: verify the explicit orderly-shutdown
   restore path and ordinary bar appearance when reopening. This does not prove
   behavior after forced termination or a crash.
6. Compare post-stream Main/Search/Apps and Settings against the initial bar
   appearance. Log actual AppMgr results with Debug log enabled; absence of a log
   with the toggle off is not evidence that no request occurred.

Completed: local source/API/lifecycle review, online VitaSDK API check and source
implementation, followed by the user's confirmation of working infobar behavior.
No build or hardware test was run by the agent; detailed matrix results and actual
AppMgr return codes remain unreported.
