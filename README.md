# Vita Moonlight PAF

Empty native PS Vita shell for a future [vita-moonlight](https://github.com/xyzz/vita-moonlight) UI.

It uses PAF so the app looks like a system Vita program. There is no streaming, pairing, or host discovery yet — only navigation and layout.

## Architecture

Frontend is split the same way as NetStream / BetterHomebrewBrowser:

```
src/main.cpp                 PAF sysmodule + module_start
src/paf_sample.cpp           Framework bootstrap, plugin load
src/pages/page.cpp           page::Base stack (open/close, SetActivate, back fade)
src/pages/page_main.cpp      hosts empty state, Search / Add / ...
src/pages/page_search.cpp
src/pages/page_add_host.cpp
src/option_menu.cpp          overflow balloon from ...
src/moonlight/settings.cpp   Vita Sce::AppSettings integration
src/moonlight/api.cpp        thin C API for the Moonlight core
```

`page_main` stays at the bottom of the stack. Overlays disable the page underneath. Circle / the bottom-left corner button closes the top page.

Settings use the Vita system `SceAppSettings` service, the same model used by NetStream and BetterHomebrewBrowser. The settings UI is declared in `cxml/moonlight_settings.xml`, while `moonlight/settings.cpp` is only a thin adapter between the system service and the Moonlight API.

There is no `moonlight.conf` compatibility layer and no custom settings file parser. Application settings are stored by the system AppSettings service. The settings subsystem is initialized lazily when settings are opened or the core first reads a setting. The Moonlight core should read them through `moonlight_api_get_settings()` or the individual `moonlight_api_get_setting_value()` calls.

Streaming must not be folded into the PAF plugin. Call `moonlight_api_*` from page callbacks.

## Screens

- **Main** — title, empty-host state, Search PCs, Add Manually, settings corner button
- **Search PCs / Add Manually** — placeholder pages with a dim overlay and back
- **Settings** — system `SceAppSettings` UI opened from the `...` button

PAF layout is **center-origin** on 960×544: `(0, 0)` is the middle of the screen, `+Y` is up. CSS-style top-left coordinates will pile widgets on top of each other.

## Build

Requires [VITASDK](https://github.com/vitasdk/vitasdk), [vitasdk-paf-component](https://github.com/Princess-of-Sleeping/vitasdk-paf-component) and [psp2cxml-tool](https://github.com/Princess-of-Sleeping/psp2cxml-tool). The PAF component provides the `AppSettings` headers and stubs used by this project.

```bash
export VITASDK=/path/to/vitasdk
bash build.sh
```

The build is fail-fast: a generated `vita_moonlight_ui.rco` is required and the bundled CXML compiler cannot fall back to an unrelated sample resource.

VPK: `build/vita_moonlight_paf.vpk`

Rebuild CXML after XML edits:

```bash
bash build_cxml.sh
cd build && cmake .. && make
```

Install the VPK with VitaShell.

## Layout notes

Widget `pos` is relative to the **parent center**, not the top-left corner:

| Position | Approximate `pos` |
|---|---|
| Screen center | `0, 0` |
| Upper center | `0, 170` |
| Lower center | `0, -80` |
| Header under status bar | `0, 240` |
| List under header | `0, -96` |

`list_view` height must be a multiple of 32. Status bar uses `Framework` `graphics_option = 7`.

## PSP2Shell debug output

This branch contains temporary debug logging through `sceClibPrintf()`. When the app is running under [psp2shell](https://github.com/Cpasjuste/psp2shell), these messages are printed to the psp2shell console.

Log lines use the `[PSP2SHELL]` prefix so they are easy to filter. The current debug points cover:

- module startup and PAF sysmodule loading
- PAF framework/resource/plugin startup
- `MoonlightApp` initialization and shutdown
- legacy adapter calls and return codes
- Moonlight backend events

Typical output starts like:

```text
[PSP2SHELL] module_start(args=0x..., argp=...)
[PSP2SHELL] PAF load: res=0x0 load_res=0x0
[PSP2SHELL] Starting PAF runtime...
[PSP2SHELL] paf_runtime_main: begin
[PSP2SHELL] Framework::SampleInit
...
```

These logs are intended for development/troubleshooting and can be removed or reduced before release.

## License

Part of the Vita Moonlight ecosystem. Add a LICENSE before shipping a release.
