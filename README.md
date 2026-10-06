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
- Inline manual PC address input with the native Vita IME.
- Native system connection loading and error dialogs.
- System-style title bars and list views based on the patterns used by Vita system applications and `GrapheneCt/NetStream`.
- LAN/mDNS PC discovery.
- Saved PC list.
- Moonlight PIN pairing.
- Retrieving the application list from a paired PC.
- H.264 video streaming and Vita AVC presentation.
- Opus audio playback.
- Vita controller input.
- Front touchscreen input.
- Motion/gyro input path.
- PS Button/application lifecycle handling.
- Stable stream shutdown and cleanup.

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
- `template_list_view_generic`
- `template_list_item_generic`
- `style_text_top_title_bar`
- `style_list_view_generic`
- `style_image_button_list_button`

The main page contains a saved-PC list, an inline PC address input using the native Vita IME, Search PCs, and access to the system AppSettings UI.

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

Discovery results are temporary. Selecting a discovered PC creates or updates its persistent host record before connection/pairing. The inline manual address input is persisted only after a successful GameStream connection. Pairing updates that host record with its paired state and MAC.

The project does not provide compatibility or migration for the original Vita Moonlight per-host credential layout. Existing per-host `uniqueid.dat`, `client.pem`, `key.pem`, and `client.p12` files are not used by the new global client-identity implementation.

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

The CXML build is fail-fast: if `psp2cxml-tool` is not available, the build stops instead of using an unrelated fallback resource.

## Runtime Validation

There is currently no automated test suite.

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
- Do not casually change persistent storage formats or paths without considering compatibility and migration.

## Status

The project is an actively developed working Vita Moonlight frontend. Streaming is functional, but the UI, host-management behavior, lifecycle edge cases, and some device/input paths are still under active development.

## License

Part of the Vita Moonlight ecosystem. Add/maintain an appropriate LICENSE before publishing a release.
