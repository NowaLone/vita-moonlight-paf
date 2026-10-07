# Project Overview

Vita Moonlight PAF is a native PS Vita application built with Sony's PAF framework. It provides a system-style frontend for the Moonlight/GameStream stack and currently has working host discovery, host persistence, pairing, application listing, and game streaming.

The project deliberately keeps PAF/UI concerns separate from the streaming implementation:

PAF pages → MoonlightApp → Services → MoonlightBackend → LegacyMoonlightAdapter → legacy GameStream / Moonlight streaming code.

The long-term goal is to keep the frontend architecture native to PAF while continuing to reuse the real Moonlight/GameStream protocol and Vita-specific rendering/input implementations rather than reimplementing the protocol inside UI pages.

## Repository Structure

- `src/main.cpp` — PS Vita module entry point, PAF/sysmodule bootstrap, newlib lifecycle, and C++ allocation overrides.
- `src/app/` — PAF runtime bootstrap and `MoonlightApp`; owns backend/services and application lifecycle plus main-thread event dispatch.
- `src/pages/` — PAF page implementations and page-stack/navigation behavior.
- `src/services/` — service-layer wrappers for hosts, discovery, pairing, connections, applications, and settings.
- `src/backend/` — backend abstraction and implementations.
- `src/backend/legacy/` — ported/vendored legacy Vita Moonlight/GameStream code, including device storage, host discovery, mDNS, and GameStream.
- `src/moonlight/` — Moonlight-facing API, stream session, Vita video/audio/input/touch/motion renderers, and `SceAppSettings` integration.
- `include/` — project headers for application, backend, services, pages, and Moonlight types.
- `cxml/` — PAF CXML resources, settings XML, locale files, and UI textures.
- `third_party/moonlight-common-c/` — Moonlight common streaming transport/protocol sources used by the stream session.
- `third_party/enet/` — ENet transport used by Moonlight common.
- `third_party/inih/` — INI parser used by legacy device persistence.
- `third_party/h264bitstream/` — H.264 parsing support used by the Vita streaming path.
- `build/` — generated build output; ignored by Git.
- `build.sh` — full CXML + CMake + Make build.
- `build_cxml.sh` — converts `cxml/vita_moonlight_ui.xml` to an RCO.
- `dev-build-run.sh` — clean rebuild and SELF deployment to a PS Vita through `psp2shell_cli`.
- `exports.yml` — Vita module export configuration.
- `psvitaip.txt` — optional local Vita IP used by `dev-build-run.sh`.

## Current Functionality

The application currently provides:

- Main PC screen with native PAF title bar, saved host list, inline Add PC address input, Search PCs, and system-settings entry.
- Saved-host management is implemented in the PAF UI with Copy/Delete multi-selection: the corner button opens the current `OptionMenu`, actions enter selection mode, saved-PC rows use `template_host_list_item_select` with native PAF `paf::ui::CheckBox` controls, and a bottom action bar provides Cancel, Select All/Deselect All, and the selected action.
- mDNS/LAN host discovery.
- Persistent saved hosts.
- Pairing with a PC using the Moonlight PIN flow.
- Application list retrieval from the paired PC.
- Game streaming with working video decode/presentation and audio.
- Vita controller input.
- Front touchscreen input.
- Motion/gyro support is implemented in the input path but has not been fully hardware-verified in the current development cycle.
- PS Button/application lifecycle handling.
- Stable stream stop/cleanup, including deferred release of mapped video frame buffers.

Streaming is kept outside the PAF plugin layer. Pages call service/backend APIs; protocol, networking, decode and device I/O live below that boundary.

## Build & Development

### Requirements

The repository expects:

- VITASDK
- vitasdk-paf-component
- psp2cxml-tool
- `psp2shell_cli` for development deployment

Typical local environment:

```bash
export VITASDK=/usr/local/vitasdk
```

The CXML compiler can be supplied with `PSP2CXML_TOOL` or discovered in the fallback locations used by `build_cxml.sh`.

### Build

Full build:

```bash
bash build.sh
```

Outputs:

```
build/vita_moonlight_paf
build/vita_moonlight_paf.self
build/vita_moonlight_paf.vpk
```

Manual CXML + build:

```bash
bash build_cxml.sh
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Development rebuild/deploy:

```bash
PSVITAIP=192.168.1.123 ./dev-build-run.sh
```

Or put the Vita IP in `psvitaip.txt`.

The target application title ID is `VLMP00001`.

### Validation

There is currently no automated unit/integration test suite. Platform-specific changes are validated primarily by:

1. successful CXML/CMake build;
2. running the VPK/SELF on actual Vita hardware;
3. checking the Vita runtime log and reproducing the affected UI/network/streaming path.

CXML changes require rebuilding the RCO before the application build.

## Architecture Rules

### PAF/UI

- `page::Base` owns the page stack and generic back-button behavior.
- New pages should follow the existing `Base` pattern instead of implementing a second page stack.
- Every CXML `<style_text>` must explicitly bind its `textobj` attribute to the corresponding inner `<textobj id="...">`; do not create unbound text styles.
- Add PC uses the native `SceImeDialog` with URL input mode; do not replace this with a custom PAF keyboard. Manual connection input accepts `host`/IP with optional `:port`. The default GameStream port is 47989; IPv6 parsing is intentionally not added until the legacy GameStream URL builder supports IPv6 literals.
- `SceImeDialog` is owned by the Main page: load `SCE_SYSMODULE_IME` when Add PC is invoked, terminate the dialog before unloading the module, and poll completion from the PAF main-thread call list. The IME input buffers must outlive the dialog.
- Add PC is an inline Main-page control; there is no dedicated Add PC page.
- Current UI uses PAF CXML resources and NetStream-style generic list/title templates. The saved-host action popup is currently implemented by the project-owned `OptionMenu` / `page_settings_bubble`; it is a PAF implementation styled and positioned from observed system-app behavior, not a claim that a Sony system action-menu API has been discovered.
- Saved-host management currently follows the observed Vita system-app selection/action pattern: the standard bottom-right `corner_button` opens the current `OptionMenu`; Copy/Delete enter multi-selection mode; selection-mode rows come from `template_host_list_item_select` and expose a native `paf::ui::CheckBox`; activating a row toggles its selection while the checkbox mirrors the state; the bottom action bar provides Cancel, Select All/Deselect All, and the selected action. Do not replace this with long-press/context menus or custom per-item popups.
- `SceClipboard` is the system API used for the host Copy action. Current VitaSDK installations do not expose a dedicated public `clipboard.h`; the implementation keeps a small local ABI declaration for `sceClipboardSetText` and links `SceClipboard_stub`.
- Saved-host Delete uses `SceMessageDialog` for confirmation and removes the selected persistent records through the HostService → backend → legacy device store path.
- The PAF coordinate system is center-origin on a 960×544 screen: `(0,0)` is screen center and positive Y points upward.
- Do not move streaming implementation into PAF pages.

### Application / Services / Backend

- UI should normally use `MoonlightApp` services rather than calling legacy globals directly.
- `MoonlightApp` owns backend lifecycle and queues backend events for main-thread delivery.
- Asynchronous backend callbacks must not mutate PAF UI directly from worker threads.
- `LegacyMoonlightAdapter` is the compatibility boundary between the service/backend layer and the legacy Moonlight code.
- Pairing events use semantic `MoonlightPairingResult` values at the Moonlight API boundary. Only `MOONLIGHT_PAIRING_ERROR_INCORRECT_PIN` means the cryptographic PIN check failed; transport, protocol, security, and local/internal failures remain distinguishable.
- `MOONLIGHT_EVENT_PAIRING_FINISHED` reports pairing success independently of host-persistence errors. A persistence failure may be logged, but it must not be presented as a failed pairing operation.

### Settings

Application settings use Sony's `sce::AppSettings` service.

The settings definition lives in `cxml/moonlight_settings.xml`; `src/moonlight/settings.cpp` is an adapter around the system AppSettings service.

Do not introduce a new custom settings file or resurrect the old `moonlight.conf` settings model without an explicit architectural decision.

### Native System / VSH Integration

The project intentionally targets a more native PS Vita application experience. The preferred direction is to use Sony system UI/services where they provide real functionality instead of reimplementing the equivalent UI or behavior in PAF.

The project is intended to remain an **UNSAFE** application when using privileged Sony/VSH-facing functionality. SAFE-vs-UNSAFE AppSettings research was completed and should not be repeated: the real `sce::AppSettings` integration loads `vs0:vsh/common/app_settings.suprx` / `app_settings_plugin.rco` through PAF. On hardware, the application works through this path when built UNSAFE; the corresponding direct system-plugin load crashes in SAFE. SAFE `SceAppUtil` / `SaveSafeMemory` experiments were useful only to establish the SAFE boundary and are not an alternative architecture for the target Settings UI.

The long-term native-integration shortlist below was researched specifically for this project. An agent should consult this section before starting a new investigation so the same module survey is not repeated from scratch.

#### Highest-priority system integration

- **`sce::AppSettings` / `SceAppSettings`** — real Sony Settings UI and application settings storage. This is the required Settings implementation, not a custom replacement. Current adapter: `src/moonlight/settings.cpp`; XML definition: `cxml/moonlight_settings.xml`.
- **`SceIme` / `SceImeDialog`** — native Vita on-screen keyboard. The current Add PC control uses it directly from Main instead of implementing a custom keyboard.
- **`SceMessageDialog` / internal message-dialog support** — native confirmation, error, progress, and wait/loading dialogs. The connection flow currently uses its native WAIT dialog while connecting and an OK error dialog on failure; pairing failures use the same common-dialog family.
- **`SceNotificationUtil`** — system notifications and progress-style notifications. It is not used by the current connection flow; do not reintroduce it for connection progress unless a concrete non-modal notification use case appears.
- **`SceNetCtl`** — system network state information and callbacks. Useful for Wi-Fi/network status, reconnect handling, diagnostics, and exposing network information in UI.
- **`ScePower`** — power/idle management. Relevant to preventing unwanted suspend during streaming and integrating the existing `disable_power_save` setting. Manual clock/frequency manipulation is not planned unless profiling demonstrates a need.
- **`SceClipboard`** — system clipboard. Useful for copying/pasting PC addresses, hostnames, or other connection data.

#### Secondary system integration worth testing

- **`SceAppMgr`** — application lifecycle, system events, application launching and related app-management APIs. Particularly interesting are system-event handling, `sceAppMgrSetInfobarState`, and app launching by URI. Do not introduce AppMgr calls merely because they exist; use them for a concrete lifecycle/shell integration need.
- **`SceNetCheckDialog`** — native network-check/diagnostic dialog. Worth testing for a user-invoked network diagnostic path, but not required for the core LAN streaming flow.
- **`SceLiveArea`** — possible LiveArea integration and app launch/context information. Could be used later for richer shell-facing presentation, but is not currently part of core architecture.
- **`SceBGAppUtil`** — background-application utility. Potentially relevant to background discovery/update work, but no current feature depends on it.
- **`SceAppMgr` BGM/audio-port facilities** — potentially useful if Moonlight audio conflicts with system/background audio are observed.
- **`SceIncomingDialog`** — native incoming-event overlay. Interesting for future shell-like notifications, but there is no current Moonlight event that requires it.
- **`SceShutterSound`** — system shutter sound, only relevant if a future screenshot/capture feature needs a native sound effect.

#### Internal / reverse-engineered APIs

These are deliberately lower priority because they are more firmware-sensitive and are not equivalent to stable application APIs:

- **`SceShellUtil`** — Shell-facing integration such as shell event handlers, shell event locking, and requests to launch applications. This is one of the most interesting UNSAFE integrations for making Moonlight behave more like a first-party/system application.
- **`SceVshBridge`** — deeper VSH bridge. Potentially powerful but firmware/internal behavior makes it a last-resort integration point.
- **`SceSystemGesture`** — system touch/gesture recognition. Interesting for native edge/system gestures, but public SDK coverage is insufficient and reverse engineering is required.
- **`SceSharedFb`** — shell framebuffer access. Do not use for the normal Moonlight renderer/UI; our PAF application already owns the normal display path, and introducing SharedFb would complicate the video/UI compositing problem.
- **`SceRegistryMgr`** — system registry access. Interesting for investigating system preferences, but not a substitute for `SceAppSettings` and not currently required.

#### Native-integration design rules

- Prefer the real Sony/system component when it provides the UX or behavior we need; do not build a PAF imitation first and only investigate the system API later.
- Keep PAF pages responsible for navigation/presentation and wrap system services through the application/service layer where practical.
- System/VSH APIs that are firmware-sensitive must be isolated behind small adapters and should not leak into general business logic.
- Do not switch the project back to SAFE merely to gain compatibility; the target architecture explicitly allows UNSAFE because genuine `SceAppSettings` and selected VSH-facing integrations are desired.
- A module appearing in `SceSysmoduleModuleId` does not by itself make it a good candidate. Evaluate whether it gives Moonlight a concrete user-facing or lifecycle benefit before adding it.
- The native-integration shortlist is already researched. New investigations should focus on a concrete use case or on a specific API behavior that remains unknown, rather than repeating a broad module survey.

### Host Persistence

Host persistence is intentionally separate from AppSettings and is owned by the host-store layer.

The client identity is global to the Vita installation:

```
ux0:data/moonlight/
    client/
        uniqueid.dat
        client.pem
        key.pem
        client.p12
```

Per-host data contains only host metadata:

```
ux0:data/moonlight/
    <storage name>/
        device.ini
```

`host_id` is the GameStream/Sunshine server UUID returned by `/serverinfo`. The client never generates a host identity. Client `uniqueid.dat` and key/certificate material are never stored in a host directory.

Discovery results are temporary. Search PC selections are persisted before connection. Add Manually does not persist the entered address up front; a successful GameStream connection stores or updates the host through the server UUID returned by /serverinfo. Failed manual connections therefore do not leave a new saved host entry. Successful pairing updates the same record with paired state and MAC.

Host identity resolution is ordered as `host_id`, MAC, internal/external address, then display name. The stable server UUID is the primary persistent identity once it is known.

The project does not provide storage compatibility or migration for the original Vita Moonlight per-host credential layout. Old per-host credential files are ignored by the new client-identity code.

GameStream receives the global client-identity directory from the host store and does not construct credential paths from the selected PC.

Saved-host deletion resolves the selected `MoonlightHost` back to the exact stored device identity (`host_id`, MAC, address, or the existing name-only fallback) before removing its `device.ini` and host directory. Do not delete saved hosts by display name alone because duplicate display names are possible. The backend entry point is `DeleteHost`; the legacy store uses the matched device object to remove the exact record.

### Streaming

Keep the streaming stack below the PAF boundary.

Current streaming sources include:

- Moonlight common C streaming transport/protocol;
- ENet;
- Vita video renderer;
- Vita audio renderer;
- Vita controller/touch/motion input;
- legacy GameStream HTTPS/RTSP/control logic;
- Vita-specific stream session management.

Do not replace working protocol logic with hand-written protocol shortcuts without first checking the existing upstream/legacy implementation.

### C/C++

- Legacy low-level code is primarily C; frontend/services/adapters are primarily C++.
- C++ classes use PascalCase, source filenames use lowercase snake_case.
- Free helper functions generally use snake_case.
- C functions exposed across C/C++ boundaries must retain the required C linkage.
- The build uses `-fno-rtti`, `-fno-exceptions`, `-fshort-wchar`, warnings, and `-O0`; code must remain compatible with these settings.
- Recent commits use short imperative messages such as `Add ...`, `Fix ...`, `Implement ...`, `Restore ...`.

## Agent Guardrails

- Do not commit generated `build/` output or generated `*.vpk`, `*.self`, `*.velf`, `*.rco`, object files, or static libraries.
- Do not bypass the PAF architecture by making UI pages depend directly on legacy globals.
- Do not bypass `MoonlightApp` main-thread event dispatch.
- Treat `src/backend/legacy/` as compatibility-sensitive code.
- Do not alter persistent host/settings formats or storage locations as a convenience refactor.
- Do not commit private keys, certificates, device credentials, Vita deployment credentials, or other secrets.
- Do not silence build failures with unrelated weak symbols/stubs merely to make the build pass.
- Do not delete or rewrite vendored dependencies unless intentionally replacing them.
- Desktop compilation is not sufficient validation for Vita-specific PAF, graphics, input, networking, or lifecycle changes.
- Prefer a small, independently verifiable commit over a broad refactor.

## Known Development Caveats

- The initial Main host-list population has required deferred refresh because the PAF page can be created before its first layout pass completes.
- Host duplicate debugging should distinguish between duplicate records returned by the persistent host store and duplicate PAF ListView cells.
- Old duplicate host directories may remain on the Vita even when the in-memory host list is deduplicated.
- Current warning-only issues include a few legacy format/constness/type warnings; they are not currently blocking streaming functionality.

## TODO / Missing Information

- Document exact supported VITASDK revision.
- Document exact vitasdk-paf-component and psp2cxml-tool revisions.
- Add a reproducible dependency/setup procedure.
- Hardware-verify the saved-host selection UI: row/checkbox positioning, bottom action-bar anchors, list height in selection mode, Select All/Deselect All focus behavior, and disabled Copy/Delete state when the list is empty.
- Verify Delete confirmation and post-delete list refresh/focus on real Vita hardware.
- Verify host Copy through `SceClipboard`; add paste/input support only after confirming a concrete UI path that uses the same system clipboard.
- Add automated tests when the project architecture is stable enough to support them.
- Document preferred formatter/static-analysis tooling if one is adopted.
- Define a release procedure, including adding/maintaining the appropriate LICENSE before a release.
