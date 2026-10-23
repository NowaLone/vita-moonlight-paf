# Official Wi-Fi Settings: background AP search

Read-only firmware/resource investigation, 2026-10-23. Deliverable: the
[Russian implementation prompt](MAIN_BACKGROUND_SEARCH_PROMPT_RU.md) for the
original Main/Search UI chat. No application, resource, protocol or persistence
implementation changes are part of this investigation.

## Inputs and scope

The four supplied files match the SHA-256 values in
[SETTINGS_UI_RESEARCH.md](SETTINGS_UI_RESEARCH.md#supplied-files-and-method):

- `/home/nowaru/Desktop/system_settings_main.rco`
- `/home/nowaru/Desktop/system_settings_plugin.rco`
- `/home/nowaru/Desktop/wifi_settings_plugin.rco`
- `/home/nowaru/Desktop/eboot.bin.elf`

Reference screenshot: `/home/nowaru/Pictures/2026-10-23-022829.jpg`, 960 x 544.
Firmware version is unknown. Addresses below are virtual addresses in this ELF,
not portable Sony API entry points. RCO offsets are tree-relative, with the tree
at file offset `0x50`. Labels `hash_XXXXXXXX` describe unresolved original IDs.

Method: read ELF LOAD mappings/import tables, match import NIDs with installed
VitaSDK/PAF databases, disassemble Thumb code with the installed
`arm-vita-eabi-objdump`, parse embedded Settings XML and RCO tables in Python.
This is static evidence **[code]**, supplemented by one screenshot **[shot]**.
No firmware module was executed, and no build or hardware test was run.

## One page, two kinds of rows

**[code]** The executable itself contains `wifi_settings.xml`: the first
`<system_settings ... plugin="wifi_settings_plugin">` starts at file offset
`0x22c01a` (VA `0x8122bf3a`). This declarative Settings XML is distinct from the
PAF presentation templates in the RCO. `page_wifi_main` contains, in order:

1. `id_wifi_mode` toggle and `id_ap` Access Points label.
2. `id_searching`, an inline label using `msg_searching`.
3. Thirty saved links, `link_saved_ap_01` through `link_saved_ap_30`, each
   `height="70"`, opening `wifi_saved_ap_menu.xml`.
4. Ten scanned buttons, `btn_scanned_ap_01` through `btn_scanned_ap_10`, each
   `height="70"`. Buttons 11–16 appear only inside an XML comment and are inactive.
5. Automatic/manual registration controls and other Wi-Fi settings.

Thus saved profiles and discovered APs share one Settings page but have distinct
row identities and activation paths. The XML alone does not prove the exact
sorting/deduplication algorithm of the dynamic data. Its fixed slot counts and
70 px row heights are specific to Settings; they are not Moonlight requirements.

The `id_searching` visibility branch at `0x8113a838..0x8113a878` checks the scanned
collection (`0x81128cec`), saved collection (`0x81126ca8`) and the Wi-Fi-enabled
flag (`0x81126fb4`). It requests visibility only when both collections are empty
and Wi-Fi is enabled, after the earlier page/environment eligibility checks.
The adjacent Japanese XML comment describes the same empty-list condition. The
branch, rather than the comment alone, supports this finding. This is inline
content, not a modal search page.

**[shot]** The supplied image shows the Wi-Fi summary, Access Points heading,
and AP rows together. It cannot establish refresh timing, responsiveness, focus
retention, or behavior when an AP disappears.

## Search: one-shot scheduling and asynchronous completion

**[code]** The relevant chain is:

1. The `id_ap` setup path at `0x8113a98e..0x8113aa22` installs callback addresses
   through `0x811274b4`. It can schedule both the search and connection-status
   callbacks with an initial PAF timeout argument of `0.0`.
2. Search scheduling at `0x8113839e..0x81138448` checks whether timer
   `0x01000001` is already installed on its handler before adding another. It
   resets the search state through `0x811274dc`, binds callback `0x81127e2f`
   (Thumb address), and calls imported
   `paf::ui::Handler::SetTimerListener` at `0x81138440`.
3. The installed PAF header defines `CB_TIMEOUT=0x01000000` and
   `CB_INTERVAL=0x02000000`. This is therefore a **one-shot timeout**, not a
   continuously firing interval event.
4. The timeout callback at `0x81127e2e..0x81127e68` checks cancellation/activity
   state and invokes `0x811274f4`. That routine prepares the next scan phase,
   including batches of saved SSIDs where applicable, and submits through
   `0x8112d55c -> 0x811401cc -> SceNetCtl NID 0x6a56fc3a`.
5. Wi-Fi initialization registers NetCtl callbacks and a PAF main-thread callback
   (`0x8112d260..0x8112d2a4`). The callback dispatch path at
   `0x8112d412..0x8112d42a` forwards through the owned handler. A controller event
   with value 4 reaches the result handler at
   `0x8113948a..0x8113948e -> 0x811277fa`.
6. The result handler checks the native operation result and retrieves bounded
   result batches. The calls at `0x811278b2` and `0x811278ea` go through
   `0x811401e6` / `0x811401f6` to private NetCtl imports
   `0xa840c74c` / `0x484ee0b2`. It updates the saved/discovered collections and
   completes the selected scan phase. Saved-profile batches may require more
   than one native request; one timeout does not imply exactly one request.
7. Completion callback `0x8113a2e8..0x8113a326` requests row updates, checks the
   search state and enabled flag, then rearms the search timeout. Instructions
   `0x8113a314..0x8113a31c` construct `0x459c4000`, the IEEE-754 float **5000.0**,
   and pass it to the scheduler at `0x8113a322`.

The supported interpretation is a five-second PAF timeout **after cycle
completion**, subject to state/enabled checks, rather than a promise to start a
new scan on exact five-second wall-clock boundaries. Native scan duration adds
to that spacing. No overlapping periodic timer is introduced by this chain.
The static code separates request from completion; native call latency and
end-to-end UI responsiveness were not measured.

Connection/status polling is a separate timer, `0x01000002`, scheduled by
`0x8113844c`. Its callback at `0x8113928c` kills that timeout, updates connection
state through `0x811377ba`, and, while enabled, rearms with **1000.0**
(`0x447a0000`, constructed at `0x811392aa..0x811392b0`). Do not conflate the
five-second discovery cycle with connection-state indication or transplant
Sony's one-second status polling into Moonlight's TCP availability service.

## Busy indication, row updates and teardown

**[code]** The AP setup path calls `0x8112317c` and stores an optional local busy
widget. That function opens Wi-Fi RCO template `hash_8eab1507` (tree `0x1744`)
under the supplied row widget. Its child `busyindicator` (tree `0x1770`) has ID
`hash_1332d6fc`, uses style `hash_183569ba` and texture `hash_ba195a37`, and its
layout (tree `0x17bc`) specifies **32 x 32**, centered alignment, pos **307,0**.
These are local widget coordinates, not measured full-screen placement.

The scan-start callback `0x8113a0be` calls the local helper at `0x81123528`:
`Show` and imported `BusyIndicator::Start`. Completion `0x8113a2e8` calls
`0x81123578`: imported `BusyIndicator::Stop` and `Hide`. This scan indication is
an **inline PAF widget**, not `CommonGuiDialog`. The executable also uses modal
dialogs for other workflows; their presence does not make the AP-list scan modal.

The completion callback requests updates for existing named rows through
`0x8113a23e`: saved links via `0x8113a0d2` and ten scanned buttons via the calls
at `0x8113a26e..0x8113a2e2`. These reach the same unresolved
`SceSystemSettingsCore` import, NID `0xf832d8fb`. This supports named-row
invalidation/update, **not** a proven generic PAF ListView diff implementation.
The inner system-core implementation and exact focus/scroll restoration were
not recovered. Do not claim Sony preserves a particular row focus algorithm.

Timer removal is explicit: `0x81136f8c..0x81136fe2` kills search timeout
`0x01000001` and sets the search state to 4; `0x81136f34..0x81136f8a` kills
status timeout `0x01000002` and clears its enabled field. Both are called by the
page cleanup path at `0x8113a700..0x8113a73c`, and by selected connection/manual
settings transitions. An in-flight result also checks state 4 before continuing
at `0x81127822`. This is evidence of lifecycle ownership and late-result
suppression; it does not prove every native operation is cancelled immediately.

The green/dim sphere remains a separate connection-state mechanism; see the
[earlier traced state branches](SETTINGS_UI_RESEARCH.md#wi-fi-dot-connection-state-with-an-animated-intermediate-state).
Neither the green sphere nor its blink effect is the AP scan progress indicator.

## Transfer to Moonlight: decisions, not Sony facts

The requested Main behavior is compatible with this reference: one PC page,
separate saved/temporary row state, automatic non-modal search, independently
owned availability checks, and explicit suspension on leaving the page.

The following remain Moonlight product/implementation choices:

- The requested five-second refresh policy must be documented as start-to-start
  or completion-to-start. Sony's traced cycle uses the latter; repeated mDNS
  queries in one existing worker can implement the requested five-second cadence
  without creating a thread per query. Do not silently retain today's ten-second
  one-shot scan and call it five-second refresh.
- Only saved PCs get Moonlight's availability dot. Temporary rows get neither
  that dot nor an extra “found” label, as explicitly requested by the maintainer.
  Keep the text/icon columns stable across the saved/temporary transition.
- Preserve focus/scroll by PC identity and avoid rebuilding an unchanged list.
  This is a Moonlight acceptance requirement, not a recovered Sony algorithm.
- A fifteen-second disappearance grace period was an earlier **proposal**.
  Neither Sony's AP aging/TTL nor a fifteen-second native policy was established.
  Keep temporary results stable across scan starts; state and test the chosen
  Moonlight expiry policy separately.
- Sony's inline busy widget and empty-list searching text need not be copied.
  Moonlight can retain its requested quiet background and static empty-state
  policy. Keep existing foreground connection/pairing/error behavior separate.
- Keep discovery below PAF pages, reuse the backend worker, synchronize published
  snapshots, reject obsolete generations and never join/wait for network work on
  the PAF thread. The current blocking `legacy_host_discovery_stop()` and unsafely
  shared discovered-host snapshot need attention before enabling periodic Main
  discovery; a timer around today's `Start()`/`Stop()` is insufficient.

No new private Sony Wi-Fi API is needed for GameStream/mDNS discovery. Their NIDs
above are evidence markers, not APIs to import into this project.

## Checks and limits

Read-only checks passed: all four SHA-256 values; exact parsing of the first
embedded Wi-Fi Settings XML (30 active saved links, 10 active scanned buttons,
order and 70 px heights); RCO tree boundaries/counts (221/40/265); the actual
instruction bytes constructing 5000.0 and 1000.0; screenshot dimensions.
NID mappings and call-chain instructions were inspected against the supplied
ELF and installed headers. Temporary extraction/disassembly stayed in
`/tmp/settings-search-research/`; proprietary binaries were not added to Git.

No application tests were necessary for this documentation-only change. Static
inspection does not establish hardware timing, UI responsiveness, exact focus
handling, AP expiry, firmware compatibility or the unresolved private NetCtl and
SystemSettingsCore APIs. Build/RCO/Vita validation belongs to the future
implementation and remains pending.
