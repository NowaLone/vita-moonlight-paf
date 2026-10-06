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

- Main PC screen with native PAF title bar, host list, Search PCs, Add Manually, and system-settings entry.
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
- Current UI uses PAF CXML resources and NetStream-style generic list/title templates.
- The PAF coordinate system is center-origin on a 960×544 screen: `(0,0)` is screen center and positive Y points upward.
- Do not move streaming implementation into PAF pages.

### Application / Services / Backend

- UI should normally use `MoonlightApp` services rather than calling legacy globals directly.
- `MoonlightApp` owns backend lifecycle and queues backend events for main-thread delivery.
- Asynchronous backend callbacks must not mutate PAF UI directly from worker threads.
- `LegacyMoonlightAdapter` is the compatibility boundary between the service/backend layer and the legacy Moonlight code.

### Settings

Application settings use Sony's `sce::AppSettings` service.

The settings definition lives in `cxml/moonlight_settings.xml`; `src/moonlight/settings.cpp` is an adapter around the system AppSettings service.

Do not introduce a new custom settings file or resurrect the old `moonlight.conf` settings model without an explicit architectural decision.

### Host Persistence

Host persistence is intentionally separate from AppSettings and is owned by the host-store layer.

The compatible per-host model is:

```
ux0:data/moonlight/
    <storage name>/
        device.ini
        uniqueid.dat
        pairing/key material
```

Each host has a persistent `host_id`. Display name and IP address are not the primary identity. Existing Vita Moonlight directories keep their current storage names; hosts without `host_id` are assigned one on first load and the field is written to `device.ini`. New host records use their generated `host_id` as the storage directory name.

Host identity resolution is ordered as `host_id`, MAC, internal/external address, then display name as a legacy fallback. This permits renamed hosts and IP changes without changing their credential directory.

Discovery results are temporary. When the user selects a discovered PC, the host store creates or updates its persistent record before connection. Successful pairing updates the same record with paired state and MAC.

Only the host store resolves a `MoonlightHost` to its credential directory. GameStream must receive the resolved directory from the store rather than constructing a path from the host display name.

Important: persistence is compatibility-sensitive. Do not casually change storage paths, file format, or host identity rules without considering existing Vita Moonlight data and migration.

The current store may still leave obsolete duplicate directories on disk when multiple legacy records describe the same PC; this is intentionally deferred until credential merging can be handled safely.

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
- Add automated tests when the project architecture is stable enough to support them.
- Document preferred formatter/static-analysis tooling if one is adopted.
- Define a release procedure, including adding/maintaining the appropriate LICENSE before a release.
