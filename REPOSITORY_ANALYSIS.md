# Repository analysis: vita-moonlight-paf

Source-level audit date: 2026-10-16

**Current follow-up, 2026-10-21:** this is a dated architecture baseline. Use
[the consolidated API decision](docs/SUNSHINE_API_AUDIT.md#6-completion-and-next-work) and
ROADMAP's current work decision for implementation priorities. XML parsing/cache invalidation,
Apps storage, TLS pinning and several transport/foreground ownership fixes have since landed;
their scoped evidence is in the API audit. Next implementation is reliable pairing/stream
completion delivery on event-ring overflow, before more routine teardown test variants.

## Scope

This document reviews the current `master` branch of `NowaLone/vita-moonlight-paf` as a PAF/system-UI port of Vita Moonlight. The implementation was compared with the relevant patterns in:

- `xyzz/vita-moonlight`
- `GrapheneCt/NetStream`
- `Ibrahim778/BetterHomebrewBrowser`

The audit covers PAF runtime/page architecture, async event delivery, GameStream state/lifecycle, host persistence, application lists, video/audio presentation, input, settings, and build/release risks.

This is a source review. It does not claim a clean VitaSDK build or new hardware validation beyond the device-validation notes already present in `ROADMAP.md`.

## Overall assessment

The port is already structurally sound as a PAF application. It is not just a UI shell: the current path is effectively

```
PAF pages
  -> MoonlightApp
  -> services
  -> MoonlightBackend
  -> LegacyMoonlightAdapter
  -> GameStream / Moonlight implementation
```

The strongest parts are the PAF page model, system-style dialogs/settings integration, the stream presentation path, and the recent stream/PS-button lifecycle work. The largest remaining risks are at the async boundary between PAF and the backend: shared state synchronization, event routing, fixed UI limits, and cleanup/error propagation.

## What is already implemented well

### PAF page/navigation model

`page::Base` uses a PAF page stack, disables the previous page while a child is active, and reactivates it when the child closes. This closely follows the proven BetterHomebrewBrowser page pattern while using `paf::vector` instead of the STL page stack.

The list pages use `ListView::ItemFactory`, templates, 960x80 rows, and PAF-native focus/transition behavior in the same general style as NetStream and BetterHomebrewBrowser.

The option menu is correctly treated as an application-owned PAF overlay. This matches the NetStream pattern: it is system-style UI built with PAF resources, not an undocumented Sony system option-menu API.

### Stream presentation and framebuffer lifetime

The video path is one of the most careful parts of the port:

- Vita hardware AVC decode is retained.
- Three CDRAM framebuffers are used.
- Each framebuffer is exposed to PAF through a `graph::Surface`.
- Decoder frames are handed to the PAF main thread through a single pending presentation task rather than creating an unbounded callback queue.
- The stream page detaches its texture before dropping surface references.
- Decoder framebuffer release is delayed until the PAF page is gone.
- A frame-pool generation protects a new stream from a deferred cleanup task belonging to an older stream.

This is the correct class of solution for handing externally owned decoder memory to PAF.

### System UI integration

The port makes good use of system facilities instead of recreating them:

- AppSettings for settings.
- IME for PC address and in-stream keyboard entry.
- Message dialogs for connection/pairing/delete flows.
- PAF common resources and templates.
- AppMgr overlay state to suppress host input while Quick Actions owns the screen.

The PS-button/Quick Actions work in `ROADMAP.md` also shows useful hardware-driven iteration rather than assuming undocumented input behavior from headers alone.

## High-priority findings

### 1. UI still truncates application and host lists

The backend application cache supports up to 64 entries:

```cpp
static const int MAX_APPLICATIONS = 64;
static MoonlightApplication s_applications[MAX_APPLICATIONS];
```

but `page::Apps` still has:

```cpp
static const int kMaxApps = 8;
MoonlightApplication m_apps[8];
```

and `ShowApps()` explicitly requests/clamps to eight items.

The main saved-host page and discovery page also use fixed arrays of eight hosts. The legacy device store itself is dynamically sized, and discovery can hold up to 16 hosts.

Consequences:

- hosts/apps after the first eight are invisible in the UI;
- the backend change from eight to 64 apps is not actually exposed to the user;
- the discovery-to-persistent-host canonicalization only searches the first eight saved hosts.

Recommended direction: use dynamically sized page-owned storage while keeping PAF ListView virtualization. BetterHomebrewBrowser demonstrates that PAF ListView item factories do not require a fixed eight-element backing store.

**Source follow-up, 2026-10-20:** the Apps portion above describes the audit baseline.
Apps now uses a page-owned `paf::vector` sized to the backend ready count, with a shared
64-entry backend bound and ID-based row callbacks. The fixed eight-app/context arrays
are removed. A later Vita log verifies ten apps copied into ten cells (after_delete=0);
the maintainer reports the requested >8 check OK. Normal two-PC list switching and return
are subsequently user-confirmed without a new log, as is the empty-list message without
repeated requests or a hang. The 64/65 boundary test is unverified and explicitly deferred
by the maintainer (2026-10-20). In-page refresh and concurrent host/cache checks remain pending; see `docs/UI.md` and the API audit.
Main/Search eight-host limits and their canonicalization issue remain open. No build/hardware run by the agent.

### 2. Backend/stream state is shared across threads without a real synchronization model

Several core modules use process-global state from PAF jobs, Vita threads, pthread workers, Limelight callbacks, and the PAF main thread.

Examples include:

- `src/moonlight/api.cpp`: current host, connection state, server object, application cache, stream/disconnect flags;
- `src/moonlight/stream_session.cpp`: stream context and running/stop/cancel flags;
- `src/backend/legacy/legacy_host_discovery.c`: discovery state and discovered-host array;
- `src/backend/legacy/device.c`: global `known_devices`.

Some flags are `volatile`, but `volatile` is not a thread synchronization primitive. `device.c` already contains explicit `TODO: mutex` / `FIXME: need mutex` comments.

The recent lifecycle fixes reduce practical races, but the state model is still fragile. A single lock/state owner per subsystem, or a small message-driven state machine, would make stream disconnect/restart and host-store access substantially safer.

**Maintainer direction, 2026-10-20:** foreground user actions should form sequential modal
workflows with one active operation and native WAIT during long steps. Blocking GameStream
calls stay within jobs/workers while PAF processes dialogs and lifecycle events. Centralize
operation reservation, completion and dialog ownership through the application/services;
keep the PIN step visible and release cancelled operations after local cleanup. Existing
completion events and stream transport threads remain implementation infrastructure.
Source follow-up, 2026-10-21: ConnectionService now reserves Connect/app-list/launch operations
and owns their native WAIT handoffs; completion reaches pages after WAIT teardown. Connect
and app-list results are retained independently of the event ring and delivered only after
the backend call returns. Base guards opening/back navigation while the owner is busy.
Pairing cancellation and stream-session ownership remain separate. Build and Vita checks
are pending; this narrows the foreground workflow problem, not all backend state races.

### 3. Event delivery uses one mutable page callback and a small bounded queue

`MoonlightApp` has one active UI callback:

```cpp
MoonlightAppEventCallback m_event_callback;
void *m_event_userdata;
```

Pages replace it on construction and manually restore another page's callback in destructors. This creates lifecycle coupling between `Main`, `Search`, `Pairing`, and `Apps`.

The app-level event ring has capacity 16. If it fills, `QueueEvent()` drops the event.
The 2026-10-21 source follow-up logs overflow and retains Connect/app-list job results in
ConnectionService, allowing completion delivery when the ring empties even if their event
was lost. Other events still have no such fallback; overflow/lifecycle stress is unverified.

This is risky for state transitions such as:

- connection ready/failed;
- pairing finished/failed;
- stream started/stopped/failed;
- settings closed.

Current direction: keep durable application/service operation state and let pages render on
completion of a serialized foreground step. Do not expand subscriptions merely to support
parallel foreground workflows. Stream/pairing terminal delivery and low-value discovery-event
coalescing still need focused ownership/overflow review.

**Consolidation check, 2026-10-21:** QueueEvent still logs then drops any event at capacity 16.
ConnectionService::GetCompletedJobEvent retains only Connect/Applications results; Launch is
excluded, and PairingService does not retain its terminal result. Lost STREAM_STARTED/FAILED
can therefore prevent PrepareCompletion from closing the launch busy dialog; lost STOPPED can
prevent page/buffer cleanup, and lost pairing completion can leave the PIN page waiting.
The actual overflow branch was interpreted at counts 15/16/17 for seven lifecycle event kinds
(21 serial cases); it drops all kinds at full capacity. No saturation was reproduced on Vita.
Keep this as a concrete next source correction, with bounded completion ownership/order checks,
not a broad event-bus rewrite or another protocol endpoint.

### 4. Persistence APIs can report success even when the file was not saved

`save_device_info()` returns `void`. If `fopen()` fails it only logs and returns.

Callers such as `legacy_device_store_add_host()`, `legacy_device_store_mark_paired()`, and `legacy_device_store_set_host_id()` can therefore return success after an unsuccessful write.

That can produce a host that appears correct for the current process but disappears or loses identity/pairing metadata after restart.

Recommended fix: make `save_device_info()` return an error and propagate it through the device-store API. Pairing protocol success can still remain distinct from persistence success in the UI, but storage failure must be observable.

### 5. Release builds are currently compiled with debug/no-optimization flags

`build.sh` configures CMake with `CMAKE_BUILD_TYPE=Release`, but `CMakeLists.txt` hardcodes `-g3 -O0 -fno-inline` into both C and C++ flags.

For a Vita streaming client, this should not be the final release configuration. It increases code size and can cost CPU time in exactly the paths where decode/input/network overhead matters.

Recommended direction: keep current flags for Debug/diagnostic builds, and add an actual release profile (for example `-O2` or a separately hardware-validated optimization level) while retaining symbols in an optional RelWithDebInfo build.

### 6. Repository has no LICENSE file

The root `LICENSE` file is absent. The project includes/derives code with GPLv3 notices from Vita Moonlight and other components.

Before a public/release build, add an appropriate top-level license and preserve required third-party notices. The README already calls this out; it should be treated as a release blocker rather than deferred documentation work.

## Medium-priority findings

### Async service state can drift from backend state

`HostDiscoveryService::m_running` is set when the PAF job is successfully enqueued, but it is not cleared when discovery naturally finishes or fails. A later `Start()` can therefore return success without starting a new scan.

The same general pattern appears in jobs whose `Run()` result is ignored because the backend is expected to emit an event. That works on the normal path, but invalid-state returns that do not emit an event can leave a page waiting indefinitely.

A service should own its operation state and reconcile it from backend completion, not only from enqueue success.

### AppUtil lifecycle is incomplete

`MoonlightApp::Start()` calls `sceAppUtilInit()`, but the reviewed shutdown/error paths do not call `sceAppUtilShutdown()`.

Network/sysmodule cleanup is explicit, so AppUtil should receive the same treatment, including initialization-failure paths after AppUtil has successfully started.

### Application cache should be invalidated on host/session transitions

At the audit baseline the global application cache survived host transitions.

The baseline Apps page normally waited for a fresh refresh event, but the API could still return
a previous host's cached list if queried before refresh. The recommendation was to clear the
cache when changing hosts and on shutdown/disconnect.

**Source follow-up, rechecked 2026-10-21:** invalidate_applications now clears count/valid/data
at connect, disconnect, shutdown and failed refresh. This baseline finding is source-addressed;
concurrent host/event races remain separate, and the API audit records the tested two-host flow.

### Application-list parsing is ad-hoc despite Expat already being available

At the audit baseline `moonlight_api_get_applications()` scanned XML with `strchr()`, `strstr()`,
and `atoi()`, without robust document validation/entity handling.

The baseline recommendation was to reuse the already linked Expat and legacy XML parsing
patterns for `/applist`, rather than adding another parser dependency.

**Source follow-up, rechecked 2026-10-21:** shared gamestream_xml.c now uses Expat for applist
and launch/resume status/results; invalid refreshes do not populate the cache. Actual-parser
C fixtures exist but remain uncompiled/unrun by the agent. That verification gap is distinct
from the old missing parser implementation; see API audit A08/F3.

### Full Vita Moonlight mapping support has not been ported

Current `vita_input_load_mapping()` only supports:

- `cross`
- `circle`
- `square`
- `triangle`

The original Vita Moonlight mapping implementation supports analog axes, axis inversion, D-pad, shoulder buttons, triggers, thumb buttons, and deadzone-related mapping fields.

Therefore the current `mapping_enabled` setting is only a partial compatibility feature. Either port the full mapping model or rename/document the current option so it does not imply upstream-equivalent mapping.

### Keyboard language UI and transmitted key support do not match

The settings UI offers US, German, Spanish, French, and Russian keyboard layouts. The in-stream IME can display those languages, but `send_keyboard_text()` only forwards characters in the ASCII range `0x20..0x7e`.

Non-ASCII text entered through the Russian layout, and accented characters from other layouts, is silently skipped. This should either be handled with an explicit key/text mapping strategy or documented/restricted until supported.

### Settings events contain uninitialized fields

`src/moonlight/settings.cpp::emit()` creates a local `MoonlightEvent` without zero-initializing it and only fills a subset of fields.

Current receivers may ignore the remaining fields for settings events, but the event contract should not expose uninitialized `application_id` / `pairing_pin` bytes. Use zero-initialization consistently.

### Persistent input/touch/motion resources are app-lifetime oriented

The input thread continues waking every 2 ms even when streaming input is inactive. Touch sampling and motion sampling are initialized for long-lived use and are not fully torn down between sessions.

This mirrors parts of the original Vita Moonlight model, so it is not automatically incorrect, but for a PAF frontend that can spend substantial time browsing/settings, reducing inactive polling and explicitly defining app-lifetime versus stream-lifetime resources would improve power/CPU behavior.

### Host storage names should be hardened

For newly added devices, `storage_name` can be derived from the host display name and is then used as a directory component. The reviewed path does not sanitize arbitrary display names before using them in filesystem paths.

At minimum, separate immutable filesystem identity from display name and sanitize/escape path-hostile characters. The server UUID is a better long-term identity once known.

## Upstream comparison notes

### NetStream

Useful patterns already adopted correctly:

- PAF runtime/allocator integration.
- `ListView::ItemFactory` with resource templates.
- application-owned PAF option-menu overlay.
- full-screen graphics page behavior.
- power tick during playback.

NetStream is a good reference for PAF behavior, but not for Moonlight protocol/lifecycle correctness.

### BetterHomebrewBrowser

Useful patterns:

- explicit page stack with previous-page activation control;
- dynamic backing collections for large PAF lists;
- background jobs with UI changes protected by the PAF main-thread mutex;
- PAF AppSettings/resource-driven UI conventions.

The current target page stack is already close to this model. The most useful next borrowing is scalable list storage rather than more UI restructuring.

### Vita Moonlight

The target correctly retains/adapts the important platform-specific streaming pieces: GameStream pairing/session logic, Vita AVC decode, Opus audio, controller/touch/motion behavior, and mapping concepts.

Areas where the port currently has reduced upstream behavior include full custom input mapping and keyboard/text handling. Protocol endpoint semantics such as cancel/quit/unpair should continue to be verified against current Moonlight/Sunshine behavior before adding calls by name.

## Recommended implementation order

1. **Correctness baseline:** propagate persistence write errors; clear application cache on host changes; zero-initialize all events; fix AppUtil shutdown.
2. **Remove artificial UI limits:** replace fixed 8-host/8-app page arrays with scalable owned storage and keep ListView item factories.
3. **Stabilize async architecture:** synchronize backend state, fix discovery service state, and replace the single mutable page callback/silent-drop queue with durable dispatch/state.
4. **Parser and compatibility:** move app-list parsing to Expat; decide whether to fully restore Vita Moonlight mapping; define non-ASCII keyboard behavior.
5. **Release engineering:** introduce real optimized release flags, add LICENSE/notices, and make a clean reproducible VitaSDK build a release gate.
6. **Tests:** add host/device-store tests, app-list parser tests (including >8 and malformed XML), host-address parsing tests, event-queue/state-machine tests, and mock-backend page/service tests.
7. **Hardware matrix:** after source-level fixes, re-run connect/pair/list/launch/stop/restart/disconnect, Quick Actions/PS-button flows, settings return, touch/motion, and repeated stream sessions on real hardware.

## Suggested architectural end state

Keep the current layer boundaries; they are useful. The main change should be to make them real ownership boundaries:

```
PAF pages
    |
    v
MoonlightApp
  - owns durable app/session state
  - owns event fan-out on PAF main thread
  - exposes read-only snapshots to pages
    |
    v
Services
  - own operation state/cancellation
  - never depend on a page object's lifetime
    |
    v
MoonlightBackend
    |
    v
LegacyMoonlightAdapter
  - serializes access to legacy global state
    |
    v
legacy GameStream / decoder / input
```

This preserves the current successful PAF port instead of rewriting it, while reducing the lifecycle coupling that is now responsible for most of the remaining risk.
