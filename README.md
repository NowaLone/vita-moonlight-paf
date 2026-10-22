# Vita Moonlight PAF

Native PS Vita Moonlight frontend built with Sony's PAF framework.

The project provides a system-style PAF UI on top of the real Moonlight/GameStream stack used by Vita Moonlight. It is no longer just a UI shell: host discovery, persistence, pairing, application listing, streaming, video/audio output, and Vita input paths are implemented.

## Architecture

The project is intentionally split into a frontend/service/backend stack:

```
PAF Page
  ↓
MoonlightApp
  ↓
Services
  ↓
MoonlightBackend
  ↓
LegacyMoonlightAdapter
  ↓
legacy GameStream / Moonlight streaming code
```

The PAF layer is responsible for UI and navigation. Streaming protocol, GameStream, networking, decoding, audio and device input remain below the frontend boundary.

## Features

The current application supports:

- Native PAF main screen and page navigation.
- Inline manual PC address input with the native Vita IME (`host` or `host:port`).
- Native system connection loading and error dialogs.
- System-style title bars and list views based on the patterns used by Vita system applications and `GrapheneCt/NetStream`.
- LAN/mDNS PC discovery.
- Saved PC list with native-style Delete multi-selection and native checkboxes.
- Native delete confirmation and persistent host removal.
- Moonlight PIN pairing.
- Retrieving the application list from a paired PC.
- H.264 video streaming and Vita AVC presentation.
- Opus audio playback.
- Optional stream statistics overlay: video rates, missing/local-drop estimates, RTT,
  decode/PAF queue timing and payload bitrate. The compact six-line layout and metric accuracy
  still need specific Vita validation; streaming/playback verification is recorded separately.
- Vita controller input.
- Front touchscreen input.
- Motion/gyro input path.
- Optional host-controller RGB overlay: Off, Like DualShock 4 or Like DualSense; source
  implemented in bff5712, with build/Vita appearance and host feedback validation pending.
- PS Button/application lifecycle handling.
- Session-owned stream stop/disconnect cleanup and reconnect, with scoped Vita evidence.

## Repository Layout

```
src/
  main.cpp
  app/                 PAF runtime and MoonlightApp
  pages/               PAF pages and navigation
  services/            host/discovery/pairing/connection/apps/settings
  backend/              backend abstraction and legacy adapter
  backend/legacy/       Vita Moonlight/GameStream compatibility code
  moonlight/            Moonlight API, stream session, Vita renderers/input
include/               project headers
cxml/                  PAF UI and AppSettings resources
third_party/
  moonlight-common-c/  Moonlight streaming implementation
  enet/                ENet transport
  inih/                INI parser
  h264bitstream/       H.264 parsing support
```

## UI

The UI is implemented with PAF CXML resources.

The current list/title resources intentionally follow the real patterns used by `GrapheneCt/NetStream`, including:

- `template_top_title_bar`
- `template_flow_bar` (opaque bottom bar of the PS4 Link style pairing page)
- `template_list_view_generic`
- `template_list_view_main` (main-page host list, shorter so it clears the bottom bar)
- `template_list_item_generic`
- `template_host_list_item` (saved-PC row with a title and a description)
- `template_host_list_item_select` (saved-PC row with a checkbox, used only in selection mode)
- `style_list_view_generic`
- `style_image_button_list_button`

The full UI reference (screen zones, layout language, pages, templates, styles, helpers, measured values from PS4 Link and Photos, recipes and pitfalls) is [`docs/UI.md`](docs/UI.md).

Foreground Connect, Apps loading and stream setup use PAF CommonGuiDialog's compact
`s_dialogTextSmallBusy`. Errors and Delete OK/Cancel use SceMessageDialog. Pages show static
empty-list labels and pairing instructions; no inline progress or success/status fallback.

The main page contains a saved-PC list, an inline PC address input using the native Vita IME, Search PCs, and access to the system AppSettings UI.

Saved hosts use the Vita system-app selection/action pattern. The standard bottom-right corner button opens a project-owned PAF `speech_balloon` action menu (not a discovered Sony system action-menu API) with **Delete** at the top and **Settings** at the bottom. Choosing Delete turns the saved-PC list into multi-selection mode with PAF checkboxes (tapping or activating a row toggles it) and a bottom action bar with Cancel, Select All/Deselect All, and Delete. Delete asks for confirmation through the native `SceMessageDialog`; no host-copy/clipboard action is provided. The selection UI has been verified on Vita, including popup placement, Delete/Settings focus, row/checkbox positioning, action-bar layout, Select All/Deselect All focus, disabled Delete behavior, confirmation, and post-delete list refresh/focus. Delete is disabled when there are no saved PCs or nothing is selected; its disabled appearance uses PAF `ButtonBase` state and keeps the button background black while rendering the label gray.

PAF uses a **center-origin 960×544 coordinate system**:

- `(0, 0)` is screen center;
- positive Y points upward;
- positions are relative to the parent center rather than CSS-style top-left coordinates.

## Settings

Application settings use Sony's **SceAppSettings** service.

The declarative settings UI is:

```
cxml/moonlight_settings.xml
```

The adapter is:

```
src/moonlight/settings.cpp
```

Settings such as resolution, FPS, bitrate, touch mode, controller type, gyro and other streaming/input options are stored through AppSettings.

The project does **not** use the old `moonlight.conf` settings model.

## Host Persistence

Saved PCs are intentionally separate from AppSettings. Host state is owned by the host store, while the client identity is shared by the entire Vita installation.

The storage model is:

```
ux0:data/moonlight/
    client/
        uniqueid.dat
        client.pem
        key.pem
        client.p12
    <storage name>/
        device.ini
        server.pem
```

The `client/` directory contains the single global GameStream client identity. Every PC uses the same client `uniqueid.dat` and RSA certificate/key.

Each host record stores metadata:

```
host_id
paired
internal
external
mac
port
prefer_external
```

`host_id` is the GameStream/Sunshine server UUID obtained from `/serverinfo`. It is the persistent PC identity; the display name and IP address are discovery/connection data, not client-identity storage keys.

Discovery results are temporary. Selecting a discovered PC creates or updates its persistent host record before connection/pairing. The inline manual address input (including a custom port when supplied) is persisted only after a successful GameStream connection. Pairing updates that host record with its paired state and MAC.

The host store resolves the selected record to `<storage name>/server.pem`; this is the public
Sunshine/GameStream **server** certificate, never the global Vita client certificate or key.
Only successful PIN proof and a matching final HTTPS pairing challenge establish a pin.
Subsequent HTTPS requests verify the complete paired leaf certificate. Old records without
`server.pem` require pairing again. An unreadable/malformed pin or changed server certificate
fails closed: remove that saved PC locally and add/pair it again to establish new trust.
Local deletion removes its server pin as part of the host directory; it does not revoke the
Vita identity on Sunshine. A pin persistence error is logged separately from pairing success;
trust then lasts only for the current connection and pairing is needed after restart.
The implementation requires the OpenSSL libcurl backend. On 2026-10-20 the maintainer confirmed
normal PIN pairing, Apps, application restart, reconnect without another PIN and stream launch/stop.
On the same date a mismatched valid saved server certificate was rejected with security result -10;
restoring the original allowed connection and retrieval of four apps. Other certificate/error cases
and the full TLS/runtime matrix remain pending; see the API audit.

The project does not provide compatibility or migration for the original Vita Moonlight per-host credential layout. Existing per-host `uniqueid.dat`, `client.pem`, `key.pem`, and `client.p12` files are not used by the new global client-identity implementation.

Deleting a saved PC currently removes its local metadata; Sunshine retains the Vita's paired-client
record. Remote self-unpair is requested in [ROADMAP.md](ROADMAP.md), but stock Sunshine exposes
revocation through its administrative API, not its paired streaming-client API. Administrative login
on Vita is outside the agreed scope. Until a client self-revocation protocol is available, remote
revocation is performed in Sunshine's own client-management UI.

## Build

Requirements:

- [VITASDK](https://github.com/vitasdk/vitasdk)
- [vitasdk-paf-component](https://github.com/Princess-of-Sleeping/vitasdk-paf-component)
- [psp2cxml-tool](https://github.com/Princess-of-Sleeping/psp2cxml-tool)
- `psp2shell_cli` for the development deploy script

Typical setup:

```bash
export VITASDK=/usr/local/vitasdk
```

Build both application profiles:

```bash
bash build.sh
```

This builds the ordinary profile in `build/normal/` and the system profile in
`build/system/`, with separate CMake caches and object files. CXML is compiled
once and copied to both trees. The resulting packages are published as
`build/vita_moonlight_paf-normal.vpk` and `build/vita_moonlight_paf-system.vpk`.
Matching debug ELF and SELF files have the same profile suffixes. Both packages
use title ID `VLMP00001`: installing one replaces the other.

To build only one profile, explicitly select it (maintainer commands):

```bash
bash build.sh -DMOONLIGHT_SYSTEM_APP=OFF
bash build.sh -DMOONLIGHT_SYSTEM_APP=ON
```

Single-profile builds use the legacy `build/` directory and also publish the
profile-suffixed files. Script selection follows explicit command-line arguments;
without a profile argument it always builds both, regardless of an old cache.
Other CMake arguments are forwarded to both selected builds. Heap tracing is
explicitly OFF by default for each script invocation; passing
`-DMOONLIGHT_HEAP_TRACE=ON` in a dual build enables it only for the system profile.
Direct CMake configuration retains its ordinary default/cached profile behavior.
CMake prints the selected `Moonlight launch profile`. With Debug log enabled,
Settings logs `[Launch]` with the profile and source revision.

The system profile uses `gdc` packaging, a 64 MiB SELF budget, a 6 MiB newlib heap, a 10 MiB PAF
heap and PAF application mode. UNSAFE and the existing authority/settings remain.
These are the device-tested streaming allocations, not an established minimum.
PAF surface/text pools are 4/1 MiB after the initial 8/4 MiB configuration failed
with an 8 MiB DDR-main allocation request on startup with a game resident. The
smaller pools save 7 MiB in requested pool capacity. On `7e20e94`, the user verified
Main/Settings, launch without closing the Vita game, and resuming its existing session
on the tested device with the earlier 32 MiB browsing budget. That budget did not establish
streaming support; current system defaults and successful streaming evidence use 64 MiB.
Install the complete VPK through VitaShell, then launch from LiveArea, rather than
replacing only the SELF, so the changed launch metadata is promoted. Then test
opening Moonlight with a Vita game suspended and returning to that same game session.
Physical reservation defaults to zero; the decoder supplies its own explicit main-RAM arena.

The system profile bounds common-c transport thread stacks to 128 KiB (the ordinary
profile requests 1 MiB each). `[Thread]` logs report each thread name, stack bytes
and creation result. Thread creation and streaming passed the user's two longer
sessions and reconnect test; broader endurance/compatibility testing remains open.

The earlier device test passed control-thread creation but stopped at decoder initialization:
`0x80620840` is `SCE_VIDEODEC_ERROR_NOT_GAME_BUDGET`. The standard decoder API rejects
automatic allocation with the system application budget. The current prototype uses
the ABI recovered from the user's firmware dump: explicit library/decoder contexts
through a CodecEngine pool and contiguous uncached main-RAM output buffers. It logs
queried sizes and each allocation/init result. On `8b5d9a5` with a 64 MiB budget,
the user verified image, sound, controls and resuming the suspended Vita game after
the stream. This is one-device/game validation; broader compatibility remains open.

The initial 32 MiB browsing profile had only 648 KiB free before video setup.
The system-profile default is now the tested 64 MiB. Existing cached overrides
are preserved; if an older build directory still has 32768 cached, select the
tested value explicitly (maintainer command):

```bash
bash build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_SYSTEM_MEMSIZE_KIB=65536 -DMOONLIGHT_SYSTEM_PHYCONT_KIB=0
```

Install the full VPK when changing the launch budget. The tested 960x544 stream
queried 5.5 MiB for the library and 8 MiB for decoder storage; the adapter allocated
a 15 MiB arena plus three 2 MiB output buffers. Decoder creation, GXM mapping,
1088 decoded frames, stream stop and deferred framebuffer release passed in the
reported run. Later user tests verified longer sessions, reconnect memory baselines,
Settings, Quick Actions and returning to the suspended game with tracing disabled.
Broader device/game, dialog/error-path and endurance coverage remains open.

`dev-build-run.sh -DMOONLIGHT_SYSTEM_APP=ON` forwards the option, builds the VPK and
stops with installation instructions. It deliberately skips SELF-only deployment
for this profile. Development deployment explicitly selects one profile in
`build/`, using the ordinary profile unless ON is supplied. Its clean rebuild
removes any cached selection, so supply ON on each system-profile invocation.

Enable **Debug log** in Settings for `[Memory]` snapshots, including the actual
AppMgr mode and USER_RW/PHYCONT/CDRAM budgets. The first useful snapshot is emitted
when Settings applies the logging preference; successful early bootstrap remains
quiet. The prototype and tuning parameters are documented in
[docs/SYSTEM_APP_MODE_RESEARCH.md](docs/SYSTEM_APP_MODE_RESEARCH.md#prototype-on-codexsystem-app-mode).

Repeated common-c `Waiting for IDR/RFI frame` messages are limited to one per
second for each kind, with omitted-message totals reported after transport teardown.
IDR requests, recovery/drop-limit events and other logs remain visible. This limits
console traffic during recovery without changing the transport's recovery behavior.

The longer user run stopped after 2732 decoded frames with `Video Receive: malloc()
failed`. After teardown, newlib still reported 5591728 used bytes in its 6 MiB
heap, while USER_RW had 31412 KiB free. Allocation ownership and fragmentation
required investigation at that stage; the later root cause and retest follow below.
The subsequent diagnostic run identified repeated pthread thread-record allocations:
the custom entry point skipped VitaSDK's pthread constructor. Startup now explicitly
initializes pthread after newlib and verifies stable thread identity before entering
PAF. With the same 6 MiB heap, the user's retest completed 22270 decoded frames,
manual stop, reconnect and another 18008 frames without allocation failure or
crash. Newlib used returned to 96768 / 96936 bytes after the two stops, and final
USER_RW free was identical at 32952 KiB. This verifies the reported memory-failure
fix and one reconnect on the tested device; broader compatibility remains open.
The user also verified streaming/reconnect, Settings, Quick Actions and returning
to the suspended game with heap tracing OFF and the same budgets. The ordinary
profile also passed the user's launch, Settings, stream and reconnect smoke test
after the shared pthread-bootstrap fix. Direct CMake configuration defaults to
ordinary launch; `build.sh` builds both profiles by default. The 64 MiB budget
applies only when system mode is enabled.
An optional diagnostic build records allocation call sites (maintainer command):

```bash
bash build.sh -DMOONLIGHT_SYSTEM_APP=ON -DMOONLIGHT_SYSTEM_MEMSIZE_KIB=65536 -DMOONLIGHT_SYSTEM_PHYCONT_KIB=0 -DMOONLIGHT_SYSTEM_NEWLIB_KIB=6144 -DMOONLIGHT_HEAP_TRACE=ON
```

Install this VPK and enable **Debug log**. Keep its corresponding unstripped
`build/vita_moonlight_paf.elf`. Capture `[Heap]` lines before/after setup, at
`video-receive-oom`, and after stop; they report the twelve largest tracked live
allocation sites, failure size and an address anchor for matching the ELF.
This option defaults to OFF, adds about 136 KiB of fixed BSS metadata and allocation
tracking overhead, and does not increase the heap. It records references handled
by linker wrappers; libc-internal allocations/frees may bypass them. Counters for
dropped entries, unknown frees, replaced addresses and ambiguous zero-size realloc
results describe coverage limits. Tracked bytes are not a replacement for `mallinfo`.
Disable the diagnostic with `-DMOONLIGHT_HEAP_TRACE=OFF` after collecting the log.

CMake cache options persist for manual configurations; the script explicitly
selects profiles and defaults tracing OFF. To build only the ordinary profile:

```bash
bash build.sh -DMOONLIGHT_SYSTEM_APP=OFF -DMOONLIGHT_HEAP_TRACE=OFF
```

Default dual-profile outputs:

```
build/vita_moonlight_paf-normal.elf
build/vita_moonlight_paf-normal.self
build/vita_moonlight_paf-normal.vpk
build/vita_moonlight_paf-system.elf
build/vita_moonlight_paf-system.self
build/vita_moonlight_paf-system.vpk
```

Each build tree contains an unstripped `vita_moonlight_paf.elf` with debug symbols
for tools such as Vita Crash Dump GUI. The script copies it to the matching
profile-suffixed name above. Keep the ELF with its corresponding VPK when collecting
a crash dump. Explicit single-profile/manual builds also retain the original
`build/vita_moonlight_paf.{elf,self,vpk}` paths. The extensionless executable in
each build tree remains an internal input to VitaSDK packaging helpers.

Rebuild CXML manually:

```bash
bash build_cxml.sh
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Development rebuild + deployment:

```bash
PSVITAIP=192.168.1.123 ./dev-build-run.sh
```

The deployment script uses title ID `VLMP00001`.

LiveArea images under `sce_sys/` must be 8-bit indexed PNG (color type 3). A 24-bit or 32-bit PNG installs as far as 99% and then fails with `0x8010113D`. `icon0.png` is 128×128 with no alpha. `pic0.png` is 960×544. `livearea/contents/bg0.png` is 840×500. `startup.png` is 280×158 and is the only one that may use palette transparency (`tRNS`). Re-exporting these from a normal editor without quantizing them will break installation again.

`vita_create_vpk()` explicitly includes all seven LiveArea frame images and the three
current manual pages (`001.png`–`003.png`); adding/removing files under `sce_sys/` also
requires updating this explicit packaging list. The deleted `004.png` is no longer a dependency.
Install the full rebuilt VPK when changing these assets. The template content/frame
revisions are now 2. Packaging completeness and lossless palette conversion were
checked without an agent-run build. On 2026-10-21, the maintainer confirmed the
LiveArea assets appeared after deleting the old app and installing the full VPK
again. Installation over the existing app had retained the default appearance;
the cause and a reliable update-in-place procedure remain unverified.

The CXML build is fail-fast: if `psp2cxml-tool` is not available, the build stops instead of using an unrelated fallback resource.

## Runtime Validation

The recorded bff5712 source gate has 147 non-build Python checks (Python 3 + pycparser), covering selected parser
grammar, pairing/trust, host-store/list, event and lifecycle logic with mocked platform/library
calls. See [tests/README.md](tests/README.md) for their scope and command. They do not compile
or execute the application or replace Vita tests. C/Expat parser fixtures also exist, but their
compilation/execution by the maintainer remains unverified.

Changes are validated primarily on real PS Vita hardware:

1. build the VPK/SELF;
2. deploy to the Vita;
3. reproduce the affected UI, discovery, pairing, or streaming path;
4. inspect the runtime log.

For streaming-related changes, verify at least connection setup, video/audio, input, and clean stream shutdown.

## Development Principles

- Reuse real upstream/legacy Moonlight logic where possible.
- Keep PAF UI independent from streaming implementation.
- Route application behavior through `MoonlightApp`, services, backend interfaces, and the legacy adapter.
- Deliver asynchronous backend events to PAF on the main thread.
- Prefer small, verifiable commits.
- Prefer real Vita system interaction patterns over invented PAF-only UX when a system-app equivalent is known.
- Do not replace the saved-host selection/action mode with long-press/context menus.
- Do not casually change persistent storage formats or paths without considering compatibility and migration.

## Development Roadmap

The next independent development task is to record exact toolchain/dependency revisions and a
reproducible setup procedure. Broader lifecycle/motion/security/error and release checks remain
open; ordinary API paths are not reopened merely because exhaustive validation is incomplete.
The canonical plan and completion criteria are in [`ROADMAP.md`](ROADMAP.md).

## Status

The project is an actively developed working Vita Moonlight frontend. Streaming is functional, but the UI, host-management behavior, lifecycle edge cases, and some device/input paths are still under active development.

As of 2026-10-22 the [client API audit](docs/SUNSHINE_API_AUDIT.md#6-completion-and-next-work)
is complete as an inventory/source comparison, with scoped Vita evidence for the normal client
flow. It is not full compatibility/security/release certification. Latest system build 5db31a9
logs typed wrong-PIN rejection, two separate Error 409 attempts, then successful pairing/Apps.
Remote self-unpair remains blocked by the absent Sunshine client operation under the agreed
no-admin-login constraint. Missing/unverified features and accepted limits are listed in the audit.

## License

Part of the Vita Moonlight ecosystem. Add/maintain an appropriate LICENSE before publishing a release.
