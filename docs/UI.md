# UI guide

How the Vita Moonlight PAF interface is built, which rules keep it looking like a native Vita app, and the
measured numbers behind every layout decision. Read this before changing anything under `cxml/`, `src/pages/`,
`src/option_menu.cpp` or `include/pages/`.

Companion documents: `AGENTS.md` (hard rules for agents), `ROADMAP.md` (plan and hardware checks), `README.md`
(overview and build).

**Current-state review, 2026-10-22:** foreground progress uses PAF CommonGuiDialog
`s_dialogTextSmallBusy`; errors/Delete confirmation use SceMessageDialog. The maintainer
accepted the compact busy and pairing appearance; ordinary handoffs/stop have scoped logs.
Main/Search/Apps use page-owned dynamic snapshots (backend discovery 16/apps 64). Typed pairing
errors and later wrong-PIN → separate 409 attempts → successful pairing/Apps are recorded in
API audit §6.10. Dated investigations below preserve earlier implementations, failed experiments
and test counts; obsolete progress APIs/diagnostics and “next” requests are historical.

System infobar during streaming: [research and implementation notes](STREAM_INFOBAR_RESEARCH.md)
compare NetStream's AppMgr calls with the Stream lifecycle **[code]**. A valid Stream page requests
hidden through `MoonlightApp::HideStreamInfobar()`; its destructor requests visible before returning
to Apps. `MoonlightApp::Shutdown()` also restores the bar independently of stop-event delivery.
Failed restores remain pending for the next cleanup request; requests/results use console-only
`[Infobar]` diagnostics. No per-frame requests or local controls/touch bindings are added. User
confirmed the feature works on Vita on 2026-10-19 after commit `85899c5` **[hw, user-reported]**.
Individual lifecycle/failure-case results were not supplied; the full matrix remains in `ROADMAP.md`
**[open]**. No build or hardware test was run by the agent.

The opt-in `MOONLIGHT_SYSTEM_APP` prototype switches the PAF runtime to
`Mode_Application` after `SampleInit()`, initially following BHBB **[code]**. After
the user's startup allocation failure, its surface/text-surface pools are reduced
from 8/4 MiB to 4/1 MiB to leave room for Moonlight's separate newlib heap **[code]**.
The original log does not identify the failed allocation's caller. On `7e20e94`,
the user confirms startup no longer crashes and Main/Settings work; Settings
open/close logs return USER_RW free space to its earlier value **[hw, user-reported]**.
The user also confirms launch without closing the Vita game and return to its same
session on the tested device **[hw, user-reported]**.
It retains `graphics_option=7` and the existing
CXML/page geometry. This is an experimental runtime profile; remaining native dialogs,
AppSettings persistence, infobar restoration and streaming presentation need Vita checks
**[open]**. See [system-mode notes](SYSTEM_APP_MODE_RESEARCH.md#prototype-on-codexsystem-app-mode).

Evidence tags used below:

- **[hw]** verified on a real Vita.
- **[shot]** measured from a Vita screenshot (a reference app or this app); not re-checked on hardware.
- **[code]** read from the source; not a claim about how it looks.
- **[open]** not verified; do not treat as fact.

Earlier LiveArea packaging follow-up (2026-10-21): `CMakeLists.txt` initially included the seven
`sce_sys/livearea/contents/frame*.png` images referenced by the `a5` template and
the then-four `sce_sys/manual/` pages **[code]**. `frame1.png` uses an 8-bit palette with
identical decoded RGB pixels; template content/frame revisions are 2 **[code]**.
Missing frame files were confirmed in the earlier local packages. After installing
the full updated system VPK over the existing app, the maintainer's screenshot still
showed the standard background/gate while the Guide button appeared **[shot]**.
The maintainer reported `a1` in the installed app's template and no template at the
checked `ur0:appmeta` path; the supplied archive XML matched packaged `a5` revision 2
**[code]**. After deleting the old app and installing again, the maintainer confirms
the requested LiveArea assets appeared **[hw, user-reported, tested Vita]**. This
supports stale installation/registration state, but does not establish which step
preserved it. Updating LiveArea in place remains unverified **[open]**; clean removal
is a recovery observed on this device, not a required general update procedure.
Layout and image content are unchanged.

Three-page manual packaging follow-up (2026-10-21): the maintainer's `529c3c5` asset update
removed `sce_sys/manual/004.png`, retaining `001.png`–`003.png`. Its stale explicit CMake entry
caused the reported VPK packaging failure after SELF creation. That entry is now removed
**[code]**; all 15 source-file entries in the VPK manifest exist and its manual list matches
the three current pages. No assets are restored or edited. This is a source/manifest check;
VPK packaging/install was not separately reported. The subsequent stage-4 startup-interruption
log is scoped below; other hardware checks remain **[open]**. No agent build.

## 1. Principles

1. **Look like a native Vita system app.** PAF + CXML for pages, `SceAppSettings` for settings, `SceImeDialog` for
   text input, `SceMessageDialog` for confirmations. No custom keyboard, no custom settings screen.
2. **Match references by measuring, not by eye.** The references are PS4 Link (same blue theme: flow pages, list
   screens), the system Photos app (selection mode, popup, selection bar) and the NetStream / `vitasdk-paf-component`
   samples (templates, speech balloon). Take 960 x 544 screenshots of both and compare pixels (section 7).
3. **Reuse a system `_common_*` resource only if it is known to exist and renders correctly.** The names already
   used in the XML are the only ones known to exist (section 4.6). Never invent a `_common_*` name. If no standard
   component is known, repeat the measured look with project styles and record the numbers here.
4. **XML first.** Layout and appearance live in `cxml/vita_moonlight_ui.xml`; code only fills content, state and
   events. Prefer choosing a template by state over showing or hiding widgets on a shared template.
5. **The UI never owns streaming or backend logic.** Pages call `MoonlightApp` services; backend events come back
   through the queued main-thread callback; no UI is touched from a worker thread.
6. **Say what was verified.** A number taken from a screenshot is not hardware-verified. Tag it, and update the tag
   when it is re-checked.
7. **Small, reversible changes.** One visual decision per commit; put the measured numbers and their source in the
   commit message and in this guide.

### Sequential foreground workflow — maintainer direction, 2026-10-20

**Current implementation, 2026-10-21:** the compact PAF busy-dialog follow-up below supersedes
the historical SceMsgDialog progress-window descriptions. Error/confirmation dialogs keep SceMsgDialog.
The temporary workflow/timing and job-lifetime traces described below were removed after the
maintainer's successful run; they remain documented as historical evidence, not current output.

Target: one action → native WAIT during a long step → completed operation/dialog teardown →
result or next page. Conflicting selection/navigation must stay blocked until success, failure,
or cancellation with local cleanup. A cancellation request alone does not release the operation.
Pairing keeps its visible PIN page as the interactive step, with explicit cancellation; a generic
WAIT must not hide the PIN. Normal stream transport/render/input threads continue during playback.

Blocking control requests run inside the operation's worker/job while the PAF main thread keeps
dialogs and lifecycle events responsive. Completion events are internal delivery to that thread.
This implements a sequential user workflow; adding independently overlapping foreground requests
or a new general-purpose asynchronous subscription architecture is not the target.

**Source implementation, 2026-10-21; build/Vita validation pending:** ConnectionService owns
one foreground operation across Connect, ApplicationService refresh and stream launch/setup.
It opens native WAIT before starting the operation and rejects overlap or WAIT initialization
failure. Connect/app-list jobs publish their returned result with release/acquire ordering;
their completion cannot reach the page until the backend call returns and WAIT close is requested.
The result also supplies a completion if the corresponding queued event was dropped. Launch
retains the stream worker and waits for STREAM_STARTED/FAILED/STOPPED, then requests dialog close.
Reservation is released before dispatch so Connect → Apps → app-list loading can proceed.

Base blocks page opening and ordinary Back while this owner is busy, including connection
error dismissal. Connect retains its native error dialog; the later text-policy follow-up below
moves Apps/load and launch errors into native OK dialogs too. Enqueue/start failure closes WAIT before releasing
ownership. The interactive pairing page and its explicit Back cancellation remain separate;
no generic WAIT covers its PIN. Stop/Disconnect APIs and broader lifecycle races are not covered
by this three-operation reservation. Pages still own presentation/navigation.

Diagnostics: `[Workflow] begin operation=N` / `complete operation=N ...`, with Connect=1,
Applications=2, Launch=3. Verify successive WAIT handoffs, failures, blocked Back/repeated
activation and transition into playback on Vita.

**User feedback / source follow-up, 2026-10-21:** after `e838d91`, the maintainer saw the
wait window only during stream launch, while Connect/Apps still displayed page status.
The page calls did still write progress text; they are now removed/hidden for saved/manual
Connect, Search selection, Apps loading and launch. Pairing success no longer says it is
loading apps behind the Apps page. Search `SetStatus()` explicitly shows the widget again
for error/discovery/idle states after hiding it during connection.

ConnectionService's progress dialog now uses native `SceMsgDialog` USER_MSG with
BUTTON_TYPE_NONE and a static-lifetime message for the current operation: `Connecting to PC...`,
`Loading applications...`, or `Starting stream...`. At that revision it closed on completion
through an Abort/FINISHED/Term handoff (corrected below); errors/empty lists remained page state and
the PIN remains visible. The installed VitaSDK header defines this mode and button type;
this replaces the generic system WAIT text rather than adding a custom PAF dialog.
No artificial minimum display time is added. The reason for the unseen earlier short-step
dialogs is not established without logs. This source follow-up still needs build/Vita checks,
including the no-button dialog's appearance and immediate Connect → Apps handoff.

### Page text and native messages — maintainer decision, 2026-10-21

**[code; build/hardware pending]** Main/Search/Apps now contain static empty-list labels only,
shown when their list is empty (hidden during foreground progress/selection). No inline
search/keyboard/connection progress, `Select a PC`, delete-success result, `Connected`, or
`Stream stopped` text remains. CXML defaults also use empty-state labels; Apps no longer has
`Loading apps...` as its resource default. Pairing uses static instructions and the PIN only.

Errors use native OK dialogs: ConnectionService reuses its existing persistent message buffer,
WAIT teardown and dialog poll for Main/Search/Apps errors. Connection failures are owned by
the service; pages restore their empty-state visibility without showing a duplicate error.
A displayed/pending message is not overwritten by another error; that notification is logged.
Busy native dialog initialization waits for the existing dialog to release; other initialization
failures are log-only. No error text is drawn on a page if dialog initialization fails.
Invalid manual input waits for the error dialog to close before retrying IME.

Pairing retains its specific error messages/server Error 409 and OK → parent behavior; local
page-open/internal errors now use that same native dialog path. Failure to initialize it is
console-only. Instructions never change to `Pairing...` or `Paired`. Stream's opt-in statistics
and video status overlay are unchanged. Delete confirmation now uses OK/Cancel: OK deletes,
Cancel keeps selection, success returns to the refreshed list, failure opens a native OK error.

**PS4 Link reference [shot]:** user-supplied 960×544 JPEG, 2026-10-21. Pixel threshold measurement
of the bright text gives first-line ink y≈98–117, second y≈138–157; the next paragraph starts
y≈217–237, followed by y≈257–277 and y≈298–317 (descenders/compression add edge pixels).
Left ink begins x≈65–67, consistent with the existing x=64 text origin. This corroborates
40 px line spacing and a second paragraph 120 px below the first. The existing shared
`style_text_paragraph` supplies 26 px regular native PAF text, opaque white, 40 px pitch,
790 px wrap width. Font family/size cannot be established exactly from JPEG pixels alone;
the actual Vita rendering still needs comparison with the reference **[open]**.

Pairing uses that same paragraph style at layout origins (64,88) and (64,208), with localized
Sunshine-specific two-line instructions. PIN moves below the instructions to a 490×68 widget
at (64,368); the existing 48 px bold PIN style and bottom Back bar remain. Those positions are
source choices **[code]**, not measurements of Moonlight running on Vita. Validate wrapping,
PIN/bar separation, no status flicker, dialog error handoffs and Delete OK/Cancel on hardware.

**Pairing appearance [hw, user-reported, 2026-10-21]:** the maintainer confirms the new
pairing screen looks excellent on the tested Vita. Keep this layout. No new Moonlight screenshot
or installed revision/profile is supplied, so this is appearance acceptance on one device,
not a measured pixel match or verification of every pairing/error state.

### Short progress-dialog investigation — 2026-10-21

The supplied Vita log shows successful native Init → connect result=0 → Term → operation 1
completion, then Init → applist count=4 → Term → operation 2 completion. Operation 3 reaches
stream setup/decoded video and the maintainer sees `Starting stream...`; the first two windows
remain unseen. No init failure is logged. The trace has no timestamps or dialog polling status,
so it establishes successful API lifetimes, not visible presentation or their durations.

**Source defect / minimal follow-up [code; Vita pending]:** normal operation completion called
`sceMsgDialogAbort()` unconditionally and ignored its result. The installed VitaSDK exposes
[`sceMsgDialogClose()`](https://docs.vitasdk.org/group__SceMessageDialogUser.html) separately.
The local SDK `api_message_dialog/user_message` sample closes BUTTON_TYPE_NONE with Close,
and the system-message example does the same for its wait dialog. NetStream/BHBB also use
normal Close for their native PAF CommonGuiDialog waits; that is a different API and is not
copied into this SceMsgDialog path. These references support normal closing, not a guarantee
about animation/visibility on this firmware.

ConnectionService now requests Close in RUNNING, waits for FINISHED, checks Term, and retains
operation ownership until Term succeeds. A request made before RUNNING is retried by the
existing PAF poll. Close errors remain pending and are logged without repeated identical logs;
Abort remains only for forced shutdown. Polling is registered at successful Init and retained
while the window is open. No minimum duration, sleeps, extra UI or protocol changes are added.

Targeted console diagnostics emit `dialog open`, `dialog first poll`, `dialog close` and
`dialog term` with operation/status/API result and elapsed_ms. There is one first-poll message
per operation, not a frame heartbeat. These distinguish a short lifetime from a late first PAF
poll; RUNNING alone still does not prove a frame was displayed. Repeat Connect → Apps → launch
and collect these lines. The visibility root cause and the effect of normal Close remain
unverified until that Vita follow-up. Eight existing Python checks do not execute these dialogs;
no build or hardware run was performed by the agent.

### Compact PAF busy dialog — source follow-up, 2026-10-21

**Evidence:** the next Vita log measures Connect first poll at 15 ms, close request at 1750 ms,
Term at 2602 ms; Apps first poll at 2 ms, close at 1531 ms, Term at 2383 ms. Both have successful
Close/Term, with about 850 ms from close request to termination. The maintainer sees the windows
late, just before closing. Opening/render-ready time is still not directly measured; RUNNING
does not establish visible presentation. This evidence does not prove all subsequent PAF frames
were responsive or identify a specific opening-animation duration.

**Maintainer-approved change [code; build/Vita pending]:** Connect, Apps loading and stream setup
now use `sce::CommonGuiDialog::Param::s_dialogTextSmallBusy` through ConnectionService. This is
the small native PAF spinner/message box without Cancel shown in the supplied BHBB screenshot.
The reference is local BHBB `9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5`,
[`common/dialog.cpp`](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/common/dialog.cpp)
(`OpenPleaseWait` without Cancel and `Close`), corroborated by NetStream's same helpers. The
large official-app screenshot resembles a cancellable busy dialog; its exact API is unconfirmed.

Before Show, the service checks/loads `SCE_SYSMODULE_INTERNAL_COMMON_GUI_DIALOG`. The module
stays loaded for the application/PAF lifetime, as in the references; its stub was already linked.
Show uses `g_plugin` and PAF wide-string content on the main thread. A failed module load or Show
rejects starting the network operation and logs the failure; Connect reports a native progress-dialog
error instead of suggesting a PC/network failure. The existing reservation, worker
result publication and main-thread event delivery remain. Messages still describe Connect,
Apps and stream start; no custom CXML imitation or minimum display duration is introduced.

The service retains its slot while busy and calls the native PAF `Dialog::Close(slot)` once at
completion, discarding its return and releasing the logical slot, as in BHBB/NetStream.
The supplied native implementation does not produce a status code (see the ELF investigation
below); the earlier negative-result retry has been removed. PAF owns visual cleanup/animation;
logical slot release does not establish that the final closing frame has been presented.
This API does not expose SceMsgDialog's FINISHED/Term contract, so do not poll it using
SceMsgDialogGetStatus or invent an animation-ready condition.
Next-step dispatch occurs only after backend completion (Connect/apps) and logical slot release.
Shutdown also closes the owned PAF slot. No callback is attached to this noninteractive wait.

Native SceMsgDialog OK errors and Delete OK/Cancel, the accepted static pairing layout and
stream statistics are unchanged. Regression checks: compact wait appearance/latency, Connect →
Apps → launch handoffs, busy input blocking, PAF busy → Sce error handoff and shutdown. Console
diagnostics are now `[Workflow] PAF busy dialog open/first poll/close` with slot and elapsed_ms;
no progress `dialog term` is expected. Eight existing Python checks do not run PAF or compile
this API usage. Appearance, speed and cleanup on Vita remain pending maintainer build/testing.

**Header compatibility follow-up [code; build pending]:** the maintainer's build fails in
the installed `common_gui_dialog.h`: missing Sony-SDK `kernel.h`. That umbrella header also
includes `appmgr.h` and unused ErrorDialog declarations requiring a Sony-only app ID constant.
ConnectionService now includes `include/platform/paf_common_gui_dialog.h`, a narrow declaration
binding for native Param/Show/Close using VitaSDK/PAF types. Param fields, callback enum and
function signatures match the installed upstream header; `arm-vita-eabi-nm -C` confirms the
Show callback signature, Close(int) and s_dialogTextSmallBusy symbols in the linked stub archive.
No fake implementation, replacement symbol, SDK edit or global compatibility headers are added.
This read-only symbol/header check is not compilation/link validation. The maintainer must
rebuild before the compact window can be tested on Vita.

**Compact busy appearance / launch [hw, user/log-reported, 2026-10-21]:** the maintainer
confirms the compact dialog now looks good. The attached trace identifies `profile=system`,
`system_app=ON`, embedded revision `ca359a25edf0`. It contains operation 3 first poll at 132 ms
on slot 2, close return `0xEE251690` at 5337 ms, then close return 0 at 5654 ms. Only after
that does operation 3 complete and StreamPage open. Launch/setup result=0, audio packets and
decoded video follow; PS double press stops the stream and all three frame buffers unmap
with result=0. This supports appearance acceptance and the tested launch/play/stop handoff.

The first Close return is negative when interpreted as int32, but no named native error or
root cause has been established. Return-value semantics and the repeated Close need further
review; do not label it a proven harmless/native error or change timing speculatively. The
trace omits Connect/Apps dialog open/close lines, so their full handoffs/latency are not newly
log-verified. This is one system-profile run, not normal-profile, failure, shutdown or stress
coverage. Its embedded revision does not establish inclusion of `279d35e` (the header fix);
no configure/build transcript is supplied. No new build or device test was run by the agent.

**Initial Close investigation [source/binary metadata; superseded below, 2026-10-21]:**
the installed ScePaf-RE header, pinned by vitasdk-paf-component to
[`b76d6ae`](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/common_gui_dialog.h),
and the newer locally available upstream revision
[`7b1d3b6`](https://github.com/GrapheneCt/ScePaf-RE/blob/7b1d3b6aca81131fd90c65b81f317909e4feda1c/include/common_gui_dialog.h)
both declare `static int32_t Close(int32_t instanceSlot)`. Neither specifies result codes or
whether another call is required. The project's narrow declaration matches that return type.

BHBB at the revision linked above and
[NetStream `f812de8`, `source/dialog.cpp`](https://github.com/GrapheneCt/NetStream/blob/f812de87f739a740ae71fa1c23cd5c788084530c/NetStream/source/dialog.cpp)
call Close once and clear their current slot without inspecting its result. This establishes
their usage, but does not prove that the native function returns void or that every negative
value can safely be ignored. The import metadata maps Close to NID `0xBC311DC0` in
SceCommonGuiDialog (library NID `0x4B4AAE97`); the installed stub confirms the symbol
`_ZN3sce15CommonGuiDialog6Dialog5CloseEi`. A stub contains no native implementation, and
[ordinary non-template C++ symbol names omit the return type](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-function).
Thus symbol matching alone cannot verify the return contract.

Two available Vita core dumps contain that export at Thumb addresses `0xe01d4ced` and
`0xe052412d` (both text offset `0x2b6c` after clearing the Thumb bit), but neither includes
the memory page containing the function body. Their included CommonGuiDialog text pages
cover only the tail of the module. No decrypted native module was found in the inspected local
references. Read-only header/history, reference-wrapper, stub/NID and core-segment inspection
therefore cannot explain `0xEE251690` or justify changing the runtime return handling.

At this stage a decrypted native module was required; the following supplied-ELF inspection
supersedes that unresolved conclusion. No runtime change was made in the metadata-only stage.

**Supplied ELF / minimal correction [binary/source; build/Vita pending, 2026-10-21]:**
the maintainer supplied `common_gui_dialog.suprx.elf`, SHA-256
`e7c0cf0fc184f00930c345cc0d8bef7d730923da47ed82a0e529042e516efede`.
It is an ARM EABI5 ELF with executable PT_LOAD at VA `0x81000000`, file offset `0xe0`,
size `0xe66c`. The export table independently maps library `0x4B4AAE97`, function
`0xBC311DC0` to Thumb entry `0x81002b6d` (code offset `0x2b6c`). No firmware version
was supplied; these binary findings apply to this module, not every firmware.

Read-only disassembly establishes these return paths:

- Close at `0x81002b6c..0x81002ba6` looks up the slot through `0x810055ec`.
  A missing/invalid slot returns the lookup's zero, without a distinct error status.
- For an existing slot it calls `0x81006a50`, then decrements the positive internal counter
  at object offset `+8`. If the counter stays nonzero, it returns without normalizing `r0`.
- With the counter zero it updates widget state, calls the closing helper at `0x81005676`,
  and returns directly. That helper's normal epilogue at `0x81005904..0x81005914` loads the
  stack-guard value into `r0`, compares it to the saved guard and returns without replacing it.
  The mismatch path calls the fail stub at `0x8100a0d8`. Import tables identify
  `__stack_chk_guard` (SceLibKernel NID `0x93B8AA67`, referenced through `0x8100b460`) and
  `__stack_chk_fail` (NID `0x37691BF8`), matching the installed VitaSDK NID database.

Thus this Close implementation exposes internal register residue, not a meaningful signed
success/error result. The earlier `0xEE251690` is consistent with the stack-guard return path;
its exact runtime value was not independently captured from the guard. A later zero can mean
the slot is already absent. Neither return establishes visual teardown completion, and an
extra call can decrement the internal counter again if the object still exists. The previous
`result < 0` check/retry was a client defect, not evidence of a native close failure.

ConnectionService now calls Close once per owned slot and discards the return; it removes
the retry/error fields and releases unstarted operations immediately after requesting close.
The narrow header retains upstream's int32_t declaration with a discard-return warning.
Logs say `PAF busy dialog close requested` without reporting register residue as a result.
Backend-result gating, operation reservation, Show/module error checks, first-poll diagnostics,
native error/Delete dialogs and pairing layout are unchanged. No minimum delay or guessed
animation/status API is added, and the supplied native binary is not stored in the repository.

Diff/lifetime review and all eight existing non-build Python checks passed. Those checks do
not execute ARM code or PAF. Regression risk: the next step/error can now start after the
first close request, without the former accidental retry delay. Maintainer build/Vita checks
remain necessary for Connect → Apps → launch, failure-to-OK handoff, immediate job-start failure
and shutdown. Expect one close-request log per opened slot and no return-based retry; appearance
acceptance from the earlier run does not verify this new handoff behavior.

**Post-correction device trace [hw, user/log-reported, 2026-10-21]:** the new maintainer
log contains all three operations with the revised `close requested` diagnostics:

- Connect, slot 0: open at 2 ms, first poll at 15 ms, one close request at 1767 ms,
  then completion event 9/result 0.
- Applications, slot 1: open at 2 ms, first poll at 3 ms, one close request at 1547 ms,
  then event 15/result 4 and four rows (`after_delete=0`, `after_insert=4`).
- Stream setup, slot 2: open at 2 ms, first poll at 14 ms, one close request at 5187 ms,
  then event 12/result 0 and StreamPage. This run uses same-app resume, accepted RTSP,
  successful setup and the nongame decoder path in AppMgr mode 2.

There is exactly one close-request line per opened slot and no return-based close retry
in the supplied trace. This verifies the normal three-step logical handoff on the tested Vita;
the log alone does not measure visible opening/closing frames. It includes no build-revision
marker, failure-to-OK handoff, immediate job-start failure, application shutdown or repeat cycle.
These remaining checks and broader F5 races are not closed by this run.

PS double press requests stop; the owner finishes with result 0, the infobar is restored and
all three video buffers unmap with result 0. Playback quality is a separate open observation:
42 frames decode, followed by IDR waiting/drop-limit/request messages, and common-c reports no
audio traffic received. The trace contains no later decoded-frame recovery or audible-output
confirmation. Do not infer sustained video/audio success or a dialog-related cause. The ENet
interruption/control error 4 appears after explicit stop and is consistent with common-c's
`ConnectionInterrupted` → `EINTR` path (`Misc.c` / `ControlStream.c`), not evidence by itself
of an earlier network disconnect. This record changes documentation only; no agent build or
device test was run.

**Playback clarification [hw, user-reported, 2026-10-21]:** asked whether this attempt had moving
video and sound or a playback problem, the maintainer replies “всё нормально”. Observed playback
is accepted for this run. This does not explain the IDR/audio log warnings or establish sustained
quality measurements; no playback defect is confirmed and no streaming-code change is made.

**Diagnostic cleanup [code; build/Vita pending, 2026-10-21]:** at the maintainer's request,
remove temporary foreground begin/complete, busy-dialog open/first-poll/close timing traces,
their timestamp/poll fields and the now-unused process-time include. Connect/Pairing jobs no
longer print addresses or Run/Finish/destruction/cancellation lifetimes; successful enqueue
traces are removed, with enqueue failures retained. Pairing no longer logs the constructor
PIN/root or PIN-widget address. Error/fallback messages remain in Debug Log. Main/Search/Apps
list-population diagnostics, backend protocol diagnostics and streaming logs are unchanged.
Native dialog calls, operation gating, main-thread polling, result publication and pairing
cancellation are preserved; the pairing result local was used only for the removed log.
This is an output cleanup, not a new UI/protocol fix. Source/diff review and eight existing
non-build Python checks pass; no build or hardware test was run for the cleanup.

## 2. Screen geometry and the layout language

### 2.1 Zones (960 x 544, **[shot]**)

| Zone | y range | Notes |
|---|---|---|
| System status bar | 0 - 32 | drawn by the system (`graphics_option = 7`) |
| Header / title bar | 32 - 96 | `template_top_title_bar` is a 64 px plane here |
| List | 96 - 480 (384 px) or 96 - 544 (448 px) | `template_list_view_main` / `template_list_view_generic` |
| Bottom bar | 480 - 544 | 64 px; only where a page has one |
| Corner buttons | bottom-left / bottom-right, about 80 px circles | system `corner_button` styles |

Rows are 80 px tall and 960 px wide. The first row's top edge is at y = 96 and draws its own dark/light line pair
(y = 95 / 96).

### 2.2 Coordinates (**[hw]** for centre-origin, **[shot]** for the anchor values)

- PAF is **centre-origin**: with default anchor/align, `pos="0, 0"` is the centre of the parent; **+Y is up**.
- `anchor` picks the point on the parent, `align` picks the point on the widget that is placed there. Values:
  x: 0 centre, 1 left, 2 right; y: 0 centre, 1 bottom, 2 top. `pos` then offsets from that point (+x right,
  +y up, so a top-anchored widget moves down with a negative y).
- `adjust` (`layout_hint`, `layout_box`): 0 none, 2 fit to content. Patterns in use: text with `size="W, 0"` and
  `adjust="0, 2"` gets its height from its content; a button with `adjust="2, 0, 0"` and `size="0, 46, 0"` gets its
  width from its label; a `layout_box` with `adjust="2, 2"` wraps its children. `size` accepts 2 or 3 components.
- `layout_box` attributes in use: `anchor`, `align`, `pos`, `space` (gap), `adjust`, `layout_type` (1 vertical,
  2 horizontal), margins, `size`.
- At run time `SetPos(paf::math::v4(x, y, 0))` on a default-anchored widget (the speech balloon) takes the widget's
  **centre** in the same centre-origin coordinates **[shot]**.
- **Visible edge vs layout box:** a button's visible edge sits about 1.7 px inside its layout box (shadow and
  rounded texture) **[shot]**. When matching a reference, measure the visible edges on both screenshots the same way
  and subtract this inset; do not compare a layout number with a screenshot number.

### 2.3 Colour and alpha (**[shot]**, derived from measured pixels)

- In a `style_plane` the style `color` and its `planeobj` `color` **multiply, alpha included**. The same value in
  both squares it. Keep the style at `color="1, 1, 1, 1"` and put the real value in the `planeobj`.
- Blending is plain encoded-space `result = background * (1 - a) + colour * a`; there is no gamma step.
- `planeobj` colours are 0..1 (`(14, 27, 119)` is `0.055, 0.106, 0.467`).
- A 1 px plane works as a line. An RGBA PNG through `<texturetable>` and `<plane texture="...">` works
  (`bg.png`, `header_line.png`).
- The system `_common_texture_ruled_line` is almost invisible in this app (1-2 levels of change), although it is
  strong in PS4 Link. Do not rely on it for a visible line.

### 2.4 Text

- `style_text` needs its `textobj` attribute bound to the inner `<textobj id="...">`; unbound text styles are not
  allowed. `align_x` omitted is left, `1` is centre; `align_y="1"` is vertical middle; `word_wrap`, `ellipsis`,
  `enlarge_line_pitch="1"` with `line_pitch` for multi-line text.
- Font size from a reference: compare the cap height of a flat capital against known fonts in this app. 22 px gives
  17 px, 28 px gives 23 px (**[shot]**; the ratio drifts by about 1 px between sizes, so confirm by overlay).
- A text widget placed over a button (host rows) has not been confirmed on hardware to let touches reach the button
  (**[open]**, tracked in `ROADMAP.md`).

### 2.5 Identifiers and XML hygiene

- Widget ids must be unique in the resource. Style-local ids (`textobj` / `planeobj` / `bg_obj` / `label_obj` /
  `glow_obj`) repeat across styles today and that is fine.
- Templates that can appear on the same page type use **distinct child ids** (`image_button_host` vs
  `image_button_host_select`); code picks the id by mode.
- No XML comments inside elements: none exist in the project and they have not been tested with `psp2cxml-tool`.
  Put explanations in docs and commit messages.
- `cxml/locale/en.xml` contains raw `&` and `<->` in some attributes. The project's tool accepts them; a strict XML
  parser does not.

## 3. Pages

| Class (`page::Type`) | XML page | Purpose | Back mechanism |
|---|---|---|---|
| `Main` (`Type_Main`) | `page_main` | saved-PC list, inline Add PC / Search PCs, selection mode | none (bottom of the stack) |
| `Search` (`Type_Search`) | `page_search_pcs` | LAN discovery list; static text only when empty | corner button `btn_close_search` |
| `Pairing` (`Type_Pairing`) | `page_pairing` | shows the PIN, pairs as soon as it opens | bar button `btn_close_pairing` |
| `Apps` (`Type_Apps`) | `page_apps` | application list of the connected PC | corner button `btn_close_apps` |
| `Stream` (`Type_Stream`) | `page_stream` | full-screen video plane, black background, optional statistics overlay; no chrome | none (closed by stream events) |
| `OptionMenu` (`Type_OptionMenu`) | `page_settings_bubble` | speech-balloon overlay: Delete, Settings | dismiss button over the whole screen |

Navigation **[code]**: Main -> `OptionMenu` -> (Settings = the system AppSettings plugin | Delete = selection mode on
Main). Main -> Search -> connect -> Pairing (unpaired host) or Apps (paired host). Main -> Add PC (inline button,
`SceImeDialog`) -> connect -> Pairing or Apps. Pairing -> Apps. Apps -> Stream.

Successful Pairing is removed from the existing Base stack only after Apps opens successfully;
Apps then reclaims the event callback that Pairing's destructor temporarily returns to the parent.
Back from Apps returns to Search when pairing began there, or to Main for a Main/manual connection,
without revisiting the completed PIN page **[code]**. Search callback restoration clears the previous
selection guard and restores empty-state visibility so another PC can be selected **[code]**. If Apps fails
to open, Pairing stays available with an error **[code]**. User verified Search → successful Pairing →
Apps → Back → Search and selecting a PC again on 2026-10-19 **[hw, tested Vita]**. The Main/manual path,
already-paired path, Pairing cancel and stream-event regression checks remain **[open]**.
Pairing failure coverage is recorded below.

Pairing failure ends the current attempt **[code, 2026-10-20]**. After its native OK
error dialog finishes, Pairing returns to the existing parent PC page. Dismissing an
error never generates another PIN or queues another handshake; selecting a PC again
explicitly starts a new attempt. This replaces the previous automatic retry for every
non-network error, which repeatedly regenerated PINs on duplicate-session rejection.
Correct-PIN success navigation is unchanged **[code]**. User/log verified one incorrect-PIN
error → OK → Search and one duplicate-session error → Search, with new requests only
after explicit PC selection, on 2026-10-20 **[hw, tested Vita]**. After Sunshine displayed
its delayed failure, another selection produced a new PIN and waited at getservercert;
completed pairing/Apps was not yet shown at that stage. A later system 5db31a9 trace
(2026-10-22, API audit §6.10) now completes all five phases and inserts one app after wrong
PIN and two separate 409 attempts on host .154. Main return and other error types remain
**[open]**. The maintainer earlier
reports the discussed recovery flow now appears OK (2026-10-20, qualified user confirmation;
no new log or separate Apps result). Returning to Search does not cancel or reset the
host's pending pairing session.

Pairing protocol errors now display the host's GameStream XML `status_message` and
`status_code` **[code, 2026-10-20]**. For the tested duplicate-session response, the
native dialog is intended to show: `The PC returned a pairing error:` followed by
`A pairing session with this uniqueid already exists (Error 409)`. This is a server
response code, not the semantic pairing result (-1003). Wrong PIN, network, security
and internal failures retain their specific local messages. Missing server details
use the generic fallback. The maintainer confirms the duplicate-session reason with
Error 409 is visible in the Vita dialog on 2026-10-20 **[hw, user-reported]**; the
supplied log independently confirms server_status=409. Other codes and long/Unicode
message clipping/layout remain **[open]**. The OK → parent PC page behavior is unchanged.

Pairing page destruction now requests local cancellation through PairingService
**[code, 2026-10-20]**. A new connection also cancels any prior pairing before enqueue.
This is nonblocking on the UI thread; queued jobs skip work, and an active HTTP pairing
wait aborts through curl progress. It does not reset Sunshine's pending session. The
reported older-host wait behind an abandoned pairing request is F8 in the API audit.
The `e7e7166ae5db` system-profile log shows active getservercert cancellation (-11/-1006),
job Finish/destruction and successful subsequent connection/new PIN page
**[hw, tested Vita]** on 2026-10-20. Wrong-PIN and later empty-reply attempts also finish/release their jobs;
that empty reply remains accepted F6. A second-host trace also verifies local cancellation
and successful Connect, followed by a real duplicate-session 409 on immediate pairing
retry **[hw, tested Vita]**. The maintainer separately confirms ordinary correct-PIN
pairing opens Apps after the cancellation change **[hw, user-reported, 2026-10-20]**;
its successful phases are not included in that trace. Queued/later-phase cancellation,
shutdown, stress/alternating-host and normal binary-profile checks remain **[open]**.
No recurring main-thread heartbeat or changed PIN timeout is introduced.

Add PC is an inline control on Main; there is no Add PC page.

## 4. Building blocks

### 4.1 `page::Base` (`include/pages/page.h`, `src/pages/page.cpp`)

- Owns the page stack. A page is `Base(page_id, back_button_id_or_NULL, open_transition, close_transition)`.
  Opening deactivates the previous page; destroying a page closes it and reactivates the one below.
- If `back_button_id` is given, the widget with that id gets the default back callback (`CloseCurrent()`); it is shown
  when there is a previous page and hidden otherwise. A corner button or a normal button both work.
- `IsValid()` must be checked after construction (`new X(); if (!x->IsValid()) delete x;`).
- `page_stream` is opened with `graphics_flag = 0x80` and a higher draw priority (`Base` special-cases its id).
- Do not add a second page stack.

### 4.2 Templates (`<templatetable>`)

| Template | Child ids | Used by | Purpose |
|---|---|---|---|
| `template_top_title_bar` | `plane_top`, `text_top`, `plane_top_separator` | Main, Apps | left-aligned header (x = 80, `style_text_header`) over the system ruled-line separator |
| `template_list_view_generic` | `list_view_generic` (960 x 448) | Search, Apps | list that runs to the bottom of the screen |
| `template_list_view_main` | `list_view_main` (960 x 384) | Main | list that ends where the bar starts (y = 480) |
| `template_list_item_generic` | `list_item_generic`, `image_button_list_item` | Apps | single-label row |
| `template_host_list_item` | `list_item_host`, `image_button_host`, `text_host_title`, `text_host_desc` | Main, Search | saved/found PC row: title and description |
| `template_host_list_item_select` | `list_item_host_select`, `image_button_host_select`, `text_host_select_title`, `text_host_select_desc`, `checkbox_host_select` | Main (selection mode) | same row with a checkbox |
| `template_flow_bar` | `plane_flow_bar` + three edge planes | Pairing | opaque PS4 Link style bottom bar (no buttons) |

### 4.3 Styles (`<styletable>`)

Planes: `style_plane_selection_bar` (+ `_edge_dark`, `_edge_light`) for the Photos style selection bar;
`style_plane_flow_bar` (+ `_edge1/2/3`) for the PS4 Link bar; `style_plane_stream_black` for the stream backdrop.

Text: `style_text_header` (26 regular, left), `style_text_paragraph` (26 regular, line pitch 40, wrapped),
`style_text_row_title` (28 regular), `style_text_row_desc` (22, 85% white), `style_text_pin` (48 bold),
`style_text_flow_note` (22, 85% white, wrapped), `style_text_main_status` (22, centred; page status text),
`style_text_stream_statistics` (18, yellow, left/top, unwrapped, 20 px line pitch; streaming diagnostics).

The stream statistics overlay uses `plane_stream_statistics` (660 x 136, top-left offset 16/-12)
and `text_stream_statistics` (644 x 120, offset 24/-20, at most 6 explicit lines). The background is
black at alpha 0.65 with a white style multiplier. These are implementation values **[code]**,
not screenshot measurements; readability, line clipping and actual appearance need Vita verification
**[open]**. The old stray `ge>` text was removed when editing the resource.

The existing AppSettings key `show_fps` and `msg_show_fps` identifier remain; the visible label is now
`Display stream statistics`. `Stream::StatisticsTask` updates cached text/background widgets at most
once per second independently of arriving video, and is unregistered in the page destructor. With
the setting off neither the task nor the per-frame timing hooks collect statistics. The overlay
shows only video mode/payload bitrate, incoming/decode/PAF FPS, available RTT/variation, drop
percentages, average decode-call/PAF-queue timing and optional host min/max/average. Unsupported
or expired measurements are omitted. During a stall only current rates, available RTT and the
stalled-video state remain. Audio/CPU/stride/window/cumulative-counter details and permanent
`N/A` rows have been removed after the user's device screenshot showed excessive screen coverage.
The old 928 x 380 panel is superseded; the compact panel is not yet hardware-verified. PAF FPS
still measures texture submission, not physical presentation.
The setting's native AppSettings hint (`msg_show_fps_hint`) explains Video/Mbps, the three FPS
rates, RTT/variation, missing/pacer/PAF drops, average decode-call/queue timing, optional host
min/max/average and the stalled-video state. Its text and binding are source-checked; native hint
layout still needs a Vita check.
RTT is sampled outside PAF by the existing session owner, which also performs transport cleanup;
UI snapshots only copy cached statistics. There is no separate RTT thread. Queue timing includes
waiting to acquire the frame mutex.
See [STREAM_DIAGNOSTICS_PLAN.md](STREAM_DIAGNOSTICS_PLAN.md) for metric definitions and remaining work.

Buttons and lists: `style_image_button_list_button` (list row button with the empty 102 x 70 icon slot),
`style_button_option_top` / `style_button_option_bottom` (popup buttons), `style_list_view_generic`,
`style_list_view_item_generic`.

Currently unused leftovers (safe to remove when touching nearby): `style_plane_fill`, `style_text_title`,
`style_text_body`, `style_button_option_single`.

### 4.4 Textures

`_bg_texture` (`texture/bg.png`, page background), `_header_line_texture` (`texture/header_line.png`, 960 x 2 RGBA:
20% black row over 18% white row, faded in from x ~ 40 and out to ~ 900). Add a texture by copying the file into
`cxml/texture/`, adding a `<texture src id type="texture/png">` entry, and rebuilding the RCO (`build_cxml.sh`).

### 4.5 Code helpers (`include/common.h`)

| Helper | Use |
|---|---|
| `bind_decide(root, id, cb, userdata)` | attach a decide (press) callback to a child by id |
| `set_widget_focusable(widget, on)` | make a widget non-focusable (checkboxes mirror state, the row owns focus) |
| `set_button_enabled(button, enabled)` | **the** way to disable a button: native `ButtonBase::Enable()/Disable()`, disabled colour `0.55, 0.55, 0.55`, and `SetActivate()` kept in sync **[hw]**. `SetActivate()` alone changes nothing visible; fading with `Widget::SetColor()` was rejected |
| `set_child_text(parent, id, text)` | set a child Text widget's string if it exists |
| `make_open_param` / `make_close_param` | page transition parameters |

### 4.6 System resources known to exist

Only these `_common_*` names appear in the XML and are known to resolve: `_common_default_style_box`,
`_common_default_style_button`, `_common_default_style_button_transparent`, `_common_default_style_check_box`,
`_common_sound_button_back`, `_common_sound_button_enter`, `_common_sound_image_button`,
`_common_style_corner_bottom_left`, `_common_style_corner_bottom_right`, `_common_style_plane_transparent`,
`_common_texture_list_70px`, `_common_texture_list_glow_70px`, `_common_texture_option_menu_base_down`,
`_common_texture_option_menu_button_{top,bottom,single}[_glow]`, `_common_texture_ruled_line`.

### 4.7 Lists (`paf::ui::ListView`) **[code]** unless tagged

- **Main startup regression, 2026-10-22:** use an empty PAF vector followed by resize for row
  storage. The installed ScePaf-RE sized constructor computes growth from an uninitialized
  capacity; the new count constructor in Main caused a null write consistent with the supplied
  core. It is replaced by empty construction/resize **[code]**. The maintainer's `6af2e3639c69`
  system-profile log reaches a one-PC Main refresh (after_delete=0/after_insert=1), two discovery
  cycles and four Apps cells without that crash **[hw, log; scoped]**. The captured Main refresh
  follows Settings; cold-start visibility/timing and >8-PC/selection remain **[open]**.
  InitialRefreshTask, list timing and CXML are unchanged.
  [Evidence and test-model limitation](MAIN_STARTUP_CRASH.md).
- Setup per page: `SetItemFactory(new ListViewFactory(this))`, `InsertSegment(0, 1)`, `SetCellSizeDefault(0, {960, 80})`,
  `SetSegmentLayoutType(0, LAYOUT_TYPE_LIST)`. The factory opens a template into `param.parent` and takes the last child
  as the item.
- Refresh: `DeleteCell(0, 0, existing)` then `InsertCell(0, 0, count)`. **The third argument of `DeleteCell` is a
  count, not the last index** **[hw on Main]**; `existing - 1` leaves a stale cell behind (a duplicate row). Search and
  Apps use the same call. Apps has logged 10-row population and a four-row → four-row rebuild
  with after_delete=0; full Search/shrink/empty acceptance remains open.
- Cells are created lazily by the factory; do not keep widget pointers across a refresh. Main/Search own
  dynamic `paf::vector<MoonlightHost>` snapshots **[code, 2026-10-21]**, removing their eight-host cap.
  `pages/host_list.h` queries the service count and copies that many records; discovery's total-count return
  can trigger a larger retry if more PCs arrive between count/copy. Discovery's existing 16-host backend
  limit remains. A failed read preserves old cells/data; an empty successful read clears the list. Old cells
  are deleted in full before swapping in new backing vectors. Main's initial-refresh task is unchanged.
  Apps owns a `paf::vector<MoonlightApplication>` sized from the
  `APPLICATIONS_READY` count, bounded by the backend's shared `MOONLIGHT_MAX_APPLICATIONS` (64), with no
  separate eight-row cap **[code, 2026-10-20]**. It deletes old cells before replacing data and inserts exactly
  the copied count. An empty ready result shows the empty state without calling `GetAll(NULL, 0)`, which would
  start another refresh. An invalid count/cache failure shows a load error.
- Apps buttons store the application ID in their PAF name. Their callback receives the page and resolves that ID
  in its current list, following BetterHomebrewBrowser's `EntryCB`/factory pattern. It holds no pointers into
  vector elements or a fixed callback-context array. Main/Search now use page userdata and per-refresh
  row tokens in widget names, following the same BHBB `EntryCB`/factory approach. Removed-cell tokens
  resolve to no current row; buttons/checkboxes keep no pointer into a resizable vector. Main stores
  dynamic per-row selection/checkbox state, remaps selection by UUID, then MAC, then address/port or
  addressless name, and rejects conflicting stronger identities. Search resolves its selected PC against
  the complete saved snapshot by address/port, with no duplicate-name fallback **[code]**. The existing
  `template_list_item_generic`/native ListView scrolling and 80 px row geometry remain in use.
- `[Apps] ShowApps count=... cells before=... after_delete=... after_insert=...` is emitted once per successful
  list population through the existing console debug toggle. Verify `after_delete=0`, `after_insert=count`,
  scrolling/selection and correct app ID at launch on Vita. A supplied 2026-10-20 log verifies
  `count=10`, `before=0`, `after_delete=0`, `after_insert=10`; the maintainer reports the requested >8 check OK
  **[hw, tested Vita; selection user-reported]**. The fragment does not identify a launched app. Smaller
  four-row → four-row rebuild is logged in the idle DisconnectHost test **[hw, log, 2026-10-21]**;
  shrinking/empty rebuild remains **[open]**. The 64/65-app boundary is unverified and explicitly deferred by
  the maintainer (2026-10-20), rather than required as the next test. Normal switching between PCs with different lists
  and return to the first is user-confirmed on 2026-10-20 **[hw, user-reported; no new log]**: each shows its own
  apps. The empty-list state is also user-confirmed on 2026-10-20 **[hw, user-reported; no new log]**:
  `No applications available.` appears without repeated requests or a hang. These checks do not validate
  concurrent host changes or every cache race. No agent build/hardware run.
- Selection mode rebuilds the cells and picks `template_host_list_item_select` instead of `template_host_list_item`.
  The checkbox exists only in the select template.
- Host-list source checks model 40 saved/16 discovered PCs, growth during reads, shrink/empty, read failure,
  identity-preserving selection, Select/Clear All, deletion after row eight and obsolete row tokens.
  All 117 Python non-build checks pass; the new nine checks interpret actual source with mocked vectors,
  widgets and services, not real PAF/allocator/thread behavior. Maintainer build and Vita scrolling,
  selection/Delete OK/Cancel, selection of a PC past eight and return navigation remain **[open]**.
  No CXML geometry/template, backend discovery limit, storage or protocol changes.

### 4.8 Status text

- Main/Apps show only static empty-list labels in the list area; Search uses its header text for the
  empty state. Pairing keeps static instructions and the PIN. Progress/errors use native dialogs.
- On Main every return to idle goes through `Main::ShowIdleStatus()`: it shows the localized
  `msg_no_pcs` only with no hosts, outside selection/connection. The label would otherwise overlap
  the rows **[shot]**. Apps controls its empty state in `ShowApps()`.
- Main's `text_main_status` starts without a CXML label; `HideStatus()` also clears its string so
  parent/page transitions cannot expose stale empty-state text. Leaving selection mode refreshes
  hosts without an unconditional status `Show()` **[code, 2026-10-21]**.
  A failed host-store read preserves the current rows and hides the label rather than treating
  the error as an empty list **[code]**. Check startup with saved PCs,
  return from Search/Apps/Settings, selection Cancel/delete with PCs remaining, and a truly empty
  list on Vita **[open]**. No agent build or hardware test.

### 4.9 Events and threads **[code]**

Discovery lifecycle follow-up **[code, 2026-10-22]**: Search calls Start synchronously only to
start the backend-owned native scan worker. The extra PAF DiscoveryJob and stale service running
flag are removed. A start failure reaches the existing constructor's native OK dialog once;
the API does not also enqueue HOST_SCAN_FAILED for that synchronous error. HOST_SCAN_STARTED
marks a new attempt before worker startup; repeated Start during an active scan is a no-op.
Found/finished events still reach Search through MoonlightApp's main-thread queue. Stop always
reaps the backend worker (also after natural completion), and restart joins any old worker before
replacing its UID. No progress text/wait window, page navigation, scan duration or layout change.
All 126 non-build checks pass; real Search completion → Back/reopen, immediate Back/reopen,
single-error presentation and worker cleanup remain **[open]** for maintainer build/Vita testing.

Application event delivery now separates the latest HOSTS_CHANGED/SETTINGS_CHANGED snapshots
from a 16-slot ordered lifecycle FIFO **[code, 2026-10-21]**. Pairing/stream/closure and scan
boundaries/errors are never coalesced. A copied pending head remains stable while the foreground
service waits for its worker; it is acknowledged before the page callback. Producer updates do
not erase that head or a newer snapshot. The existing main-thread mutex/call list and native
dialog handoff are preserved. Snapshot bursts are source-tested; critical-only FIFO exhaustion
still logs rejection. Maintainer build/Vita smoke validation is **[open]**; no UI layout changes.

Stream presentation metadata carries a statistics-session token and queue timestamp with each
pending frame. The video bridge records queue replacement and successful PAF texture submission;
the statistics module rejects tokens from older sessions. These hooks measure CPU submission and
queue wait, not GPU completion or physical display. Source implementation only; build/hardware
validation is pending. Pages read snapshots through `MoonlightApp::Connection().GetStreamStats()`.

`PresentTask` is a persistent call-list entry registered once with the Stream instance and removed
in its destructor, like NetStream's surface update task. `QueueFrame()` only replaces the pending
frame; neither it nor the callback registers another entry. The deferred framebuffer release task
also stays registered while counting down and unregisters on completion or invalidation. PAF call
lists are persistent, so treating them as one-shot queues accumulated callbacks in the previous
source. The fix bounds registration count in source. Later owner-cleanup/restart and repeated
stream logs are scoped in the API audit; actual callback counts and attribution of the original
resource failure are not established by those traces.

Apps schedules the same generation-guarded deferred framebuffer release for STREAM_FAILED as
for STREAM_STOPPED **[code, 2026-10-21]**. Failed or cancelled setup can create decoder buffers
without publishing STARTED/opening Stream. Release remains on the main-thread call list after
common-c teardown; a pending release never frees a newer pool or a pool still referenced by an
active Stream page. This also covers a successful common-c return suppressed by a prior local
cancel, after the owner stops that transport. No new progress/error UI or inline text is added.
The later strict stage-4 cancellation trace verifies no first STARTED/Stream page, partial
common-c unwind → -7/CLOSED → native error termination → paired recovery/PASS late_start=0
and final normal stop/audio release/three unmaps **[hw, log, 2026-10-21]**. That first cancelled
setup never allocated video buffers. The earlier late_start=1 teardown test predates this change.
The existing startup test now offers stage 10 after video setup **[code, 2026-10-21]**:
it requires the old framebuffer generation to clear after FAILED/CLOSED before reconnect.
The later stage-10 trace verifies three mapped buffers/generation=1, suppressed frontend STARTED
after cancelled common-c success, owner cleanup → FAILED=-7/CLOSED → three old unmaps returning 0
before native error termination/reconnect, and same-app recovery/PASS **[hw, log, 2026-10-21]**.
Recovery decodes 445 frames; final double PS releases audio and all three buffers successfully.
The maintainer confirms image/sound on Sunshine 2025.924.154138. This covers that late cancelled-
success path on one Vita; other races, blocked-I/O latency, shutdown and repeats remain **[open]**.
See the [maintainer procedure and evidence](../tests/runtime/README.md#after-video-allocation-checkpoint).
No UI behavior is changed by this evidence record.

Three ordinary same-app start/double-PS stop cycles on system build 98f0d44194af now verify
Stream creation/infobar hide → owner stop=0/audio release → infobar restore/three successful
unmaps/deferred release before the next Start **[hw, log, 2026-10-21]**. Post-release USER_RW
free is 33596 KiB each time; newlib used rises by 464 bytes across the repeats, so this is
resource-reuse evidence rather than a leak-free proof. It does not validate host reconnect,
active application shutdown or the later ee485a2 Main-label change. See
[the recorded result](../tests/runtime/README.md#ordinary-repeated-startstop--maintainer-result-2026-10-21).

Backend callbacks may arrive on worker threads. `MoonlightApp` queues them and delivers them through
`MainThreadCallList`, so page callbacks run on the PAF main thread. Only the topmost page owns the event callback; when
a page is destroyed it hands the callback back to the page below (`RestoreEventCallback()` in `Main`, `Search`, `Apps`,
`Pairing`). Streaming stays behind the `moonlight_api_*` boundary; never move it into a page.

### 4.10 Controller RGB overlay **[code, 2026-10-22; Vita validation open]**

Input settings now offer `Controller light`: Off (default), Like DualShock 4, Like DualSense.
The choice is captured on the next stream; it changes the local effect, not the host controller
type. This is game/controller RGB feedback, not an image-derived ambient-light effect. A host
virtual controller/game must produce RGB output; no default blue or invented color is displayed.
The existing common-c `setControllerLED` callback receives controller 0 RGB. Input advertises
`LI_CCAP_RGB_LED` only for an enabled, started session. No protocol/dependency replacement or
physical Vita/controller LED API is introduced.

The stream mutex protects a small cached snapshot (generation, mode, RGB). The callback writes
only that cache, ignoring other controller IDs, Off and stopping/finished sessions. An accepted
start resets RGB to black and advances the generation; the sole owner joins transport callbacks
before another session can be admitted. The getter reports Off before STARTED and immediately
after stop/cancel/termination or completion. There is no new event type, worker, allocation,
network query, per-frame diagnostic or statistics dependency.

`ConnectionService -> backend -> Moonlight API -> stream` exposes the snapshot. Stream's existing
main-thread `PresentTask` reads it even when no video frame arrives, rejects another generation
and changes PAF widgets only when mode/RGB changes. Off pages do not poll it. Black hides the
effect, including its halo. The planes belong to the Stream root and disappear with that page;
no additional call-list registration or framebuffer ownership is added. Video scaling/cropping,
statistics, touch/controller input and infobar behavior are unchanged.

Appearance is project artwork, **not measured Sony UI or hardware-verified geometry**:

- Like DualShock 4: a **200 x 36 px [code]** white/alpha mask anchored at bottom center. Its
  light-bar lens has an almost straight bottom, rounded ends and a raised curved upper center,
  with a soft halo. Host RGB tints it. The silhouette follows the maintainer's DS4 photo:
  blue lens bounds **x=348..833, y=493..571 [shot]**, about **6.2:1 [shot]** width/height;
  the source lens spans approximately **164 x 26 px [code]** inside the existing plane.
  Per the 2026-10-23 request, the mask is rotated **180 degrees [code]** from the photo's
  orientation so the straight edge faces the screen bottom; Vita viewing remains **[open]**.
- Like DualSense: two **24 x 448 px [code]** planes flush to the left/right edges, vertically
  centered. Mirrored **16 x 128 px [code]** alpha masks fade inward and toward both ends.
- All three planes appear after video and before statistics in CXML. Both style and planeobj
  colors remain white; widget tint supplies RGB. No full-screen tint, top/bottom border, animation
  loop or input handler is added. Source masks total **45,184 uncompressed RGBA bytes [code]**;
  this is not a measurement of PAF/GPU allocation overhead.

`tools/generate_controller_light_textures.py` authors these source PNG masks with the standard
library; it does not compile resources. Ten non-build checks interpret the actual RGB callback,
accepted-start reset, snapshot getter and normalized PAF update method with serial mutex/widget
mocks. They cover filtering, stop/cancel/inactive gating, restart, black, duplicate events, stale
generation, Off/missing-widget behavior, service/capability wiring, CXML layering/localization and
reproducible alpha assets. A software composition preview checks source artwork only. CXML/C++
compilation, real callback ordering, PAF alpha/tint, AppSettings persistence and both modes over
video (including stop/restart, paused frames, crop/fit, statistics and Quick Actions) remain
**[open]** for the maintainer's build/Vita validation. No agent build or device run.

**Maintainer follow-up, 2026-10-22:** the user reports both modes work **[hw, user-reported]**
and supplies `2026-10-22-154233.jpg` showing the prior orange DS4 cap over a running stream
**[shot]**. This establishes the reported basic RGB/display path, not the entire lifetime,
settings-persistence or performance matrix above. The DS4-only source mask is now reshaped to
match the supplied real light-bar photo (`codex-clipboard-7de963d3-463a-4830-a94e-38bdd7a2016e.png`);
its plane size/anchor and DualSense assets are unchanged. Software preview and existing mask
checks verify source artwork; the revised shape still needs maintainer RCO rebuild/Vita viewing
**[open]**. No agent build or device run.

## 5. Components and their measured look

### 5.1 Header and paragraph

| Item | Value | Source |
|---|---|---|
| Flow page header (`style_text_header`) | 26 px regular, text origin x = 64, cap centre y = 61.5 | PS4 Link search page **[shot]** |
| List screen header | same style, text origin x = 80 | PS4 Link "Which feature..." **[shot]** |
| Flow header line | rows y = 95 (dark) / 96 (light), fades in x 40-260, out 700-900 | `header_line.png` **[shot]** |
| Paragraph (`style_text_paragraph`) | 26 px, line pitch 40, x = 64, first line centre y = 108, wrap width about 790 | PS4 Link pairing page **[shot]** |

On list screens the dark/light pair at y = 95 / 96 is the first row's top edge, so the shared title bar does not draw
that line itself.

### 5.2 List rows (PS4 Link "Which feature...") **[shot]**

Title 28 px regular, cap centre 25 px below the row top; description 22 px at 85% white, cap centre 58.5 px below the
row top. PS4 Link text starts at x = 220 because of its icon column; this app has no icons and starts at x = 175.
`pos` for the texts is `175, -9` (title) and `175, -45` (description).

### 5.3 Bottom bars

| Bar | Look | Source |
|---|---|---|
| Selection bar (Photos) | 64 px, top at y = 480; body about 5% darker than the page; dark row y = 480 (5% black extra), light row y = 481 (18% white) | system Photos **[shot]** |
| Flow bar (PS4 Link) | 64 px, opaque; body (14, 27, 119) from y = 480; edge rows (6, 21, 108) at 480, (83, 94, 173) at 481, (42, 53, 135) at 482 | PS4 Link **[shot]**, identical on three different pages |

The selection bar keeps the Photos values on purpose. A stronger tint (12% / 10% / 30%) was tried on the blue theme
and judged too prominent.

Bar buttons **[shot]**: 144 x 45 px visible, `adjust="2, 0, 0"` with `size="0, 46, 0"`. Left button `pos="9, 9"`. The
right group (Select All / Delete) is a horizontal `box` anchored right with `pos="-8, 9"` and `space="9"`; the visible
gap is about 12.5 px and the right edge about 10 px from the screen edge. Do not pre-fit widths: labels change
(Select All becomes "Clear All" only when every saved PC is selected, `Main::AllHostsSelected()`). Widths and
positions matched Photos to within 1 px on a screenshot; resizing when the label changes has not been confirmed
separately (**[open]**).

### 5.4 Popup (`OptionMenu`, speech balloon)

- A real vertical PAF `<speech_balloon>` in `page_settings_bubble`, texture `_common_texture_option_menu_base_down`
  (tail points at the bottom-right corner button). Actions: Delete on top (`style_button_option_top`), Settings at the
  bottom (`style_button_option_bottom`). This is a project-owned composition styled from system apps, not a discovered
  Sony action-menu API. Placement, Delete/Settings focus and the disabled Delete look are **[hw]**.
- Size: 202 x 60 px buttons, 6 px margins, so height = 60 x buttons + 12; width 214.
- Position (`option_menu.cpp`): `SetPos()` centre at x = 258 (the balloon spans x 631 - 845) and a vertical centre
  derived from the button count so the bottom edge stays 50 px above the screen bottom. The old trial
  `SetPos(420, -140)` put the balloon partly off screen; do not bring it back.
- Historical hardware observation: an earlier test of Sony `_common_template_speech_balloon_menu_3` (`0x4a34c804`)
  gave a centred horizontal menu with `SetAdjust(0, 0, 0)` and `SetPosCenter(paf::math::v4(0, 0, 0))`; `(480, 272)`
  moved it upper-right. It confirmed centre-origin coordinates for that horizontal template only. Do not apply its
  `SetPosCenter()` setup to this vertical popup, which uses `SetPos()`.
- The official `Princess-of-Sleeping/vitasdk-paf-component` sample wraps a vertical `box` with 7 px margins and
  top/middle/bottom system option-menu buttons (glow textures, `_common_sound_button_enter`) in a
  `<speech_balloon texture="_common_texture_option_menu_base_side">`. The production popup uses
  `_common_texture_option_menu_base_down` instead so the tail points toward the `...` button; it needs only the top
  and bottom button styles, the middle one is no longer needed.

### 5.5 Selection mode on Main

Popup -> Delete -> multi-selection: every row shows a checkbox (`checkbox_host_select`, 58 x 58 at `pos="92, 0"` in
the empty icon slot) that mirrors the row; activating the row toggles it; the bottom selection bar offers Cancel, Select
All / Clear all and Delete; Delete is disabled until something is selected and asks for confirmation through
`SceMessageDialog`. There is no Copy or clipboard action and no `SceClipboard` dependency; do not restore it unless
asked.

## 6. Recipes

**Add a page.** Add `<page id="page_x">` to the XML (first child: a `plane texture="_bg_texture"` of size
`960, 544`). Add a class deriving `page::Base` with `Base("page_x", "btn_close_x", open, close)`, a `Type_X` value and
`GetType()`, and add the `.cpp` to `add_executable` in `CMakeLists.txt` (sources are listed explicitly). Own the event
callback and hand it back in the destructor. Open it with `new X(); if (!x->IsValid()) delete x;`. Add strings to
`cxml/locale/en.xml`.

**Add a list row type.** Add a template with a 960 x 80 `list_item`, an `image_button` using
`style_image_button_list_button` (keep `focus_hint focus_shape="3"`), and optional `text` / `check_box` children with
ids that are unique to that template. Choose the template in the factory by state and fill it with
`set_child_text()`.

**Add a button to a bar.** Use `_common_default_style_button`, `adjust="2, 0, 0"` and `size="0, 46, 0"`, `pos y = 9`; put
two or more right-hand buttons in a horizontal `box` so they follow each other when a label changes. Use
`_common_sound_button_back` for back/cancel and `_common_sound_button_enter` otherwise.

**Disable a button.** `set_button_enabled(button, false)`. Do not use `SetActivate()` alone or `SetColor()`.

**Add a setting.** The settings UI is the system AppSettings plugin driven by `cxml/moonlight_settings.xml`:
add a `list` or `toggle_switch` entry (`id`, `title`, optional `hint`, `key`, `default_value`), its strings in
`en.xml`, a `MoonlightSettingKey` value in `include/moonlight/types.h`, the key name in `key_name()` and the field in
`moonlight_settings_get_all()` (`src/moonlight/settings.cpp`), and a field in `MoonlightSettings` if the stream needs it.
Do not introduce a settings file or a custom settings screen.

**Show a status message.** `SetStatus(...)`. Return to idle with `Main::ShowIdleStatus()` on Main.

## 7. Matching a reference screenshot

1. Use 960 x 544 screenshots of the reference screen and of this app's screen in the same state.
2. **Edges:** along a row or column that crosses the shape but not its text, take the absolute difference of
   neighbouring pixels and keep the peaks. Run the same detector on both images and compare visible edges.
3. **Positions:** convert to layout values with the known inset (section 2.2); change `pos` in 1 px steps.
4. **Colours:** sample a band that has no widgets, above and below the element, and solve
   `result = bg * (1 - a) + c * a`. Remember that style and `planeobj` colours multiply, and that JPEG blurs 1 px lines.
5. **Text size:** compare flat capital cap heights against fonts known in this app; confirm by overlaying.
6. Record the numbers and their source here and in the commit message, tagged **[shot]** until a hardware check.

## 8. Pitfalls

- `DeleteCell(0, 0, existing - 1)` leaves a stale row.
- A `style_plane` colour set in both the style and the `planeobj` is squared.
- `_common_texture_ruled_line` is nearly invisible in this app.
- `SetActivate()` does not dim a button; `Widget::SetColor()` on a disabled button was rejected in favour of the native
  disabled state.
- Showing and hiding a checkbox on a shared row template misbehaves; use the select template.
- Transient status messages still overlap the rows; the idle hint must not (use `ShowIdleStatus()`).
- Do not restore host Copy / `SceClipboard`, and do not add long-press or per-item popup menus.
- Hardware-only checks are tracked in `ROADMAP.md`; do not tick them without a Vita run.
