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
src/pages/page_settings.cpp  categories, rows, checkbox, list picker
src/option_menu.cpp          overflow balloon from ...
src/settings_model.cpp       setting values (stand-in until sce::AppSettings)
src/moonlight/api.cpp        thin C API for a future libgamestream hook
```

`page_main` stays at the bottom of the stack. Overlays disable the page underneath. Circle / the bottom-left corner button closes the top page.

Settings still use the custom list_view templates in `cxml/vita_moonlight_ui.xml` because VitaSDK does not link `SceAppSettings`. `cxml/moonlight_settings.xml` is the target schema for that move.

Streaming must not be folded into the PAF plugin. Call `moonlight_api_*` from page callbacks.

## Screens

- **Main** — title, empty-host state, Search PCs, Add Manually, settings corner button
- **Search PCs / Add Manually** — placeholder pages with a dim overlay and back
- **Settings** — overflow balloon from the `...` button, then a category list

PAF layout is **center-origin** on 960×544: `(0, 0)` is the middle of the screen, `+Y` is up. CSS-style top-left coordinates will pile widgets on top of each other.

## Build

Requires [VITASDK](https://github.com/vitasdk/vitasdk) with [vitasdk-paf-component](https://github.com/Princess-of-Sleeping/vitasdk-paf-component) and [psp2cxml-tool](https://github.com/GrapheneCt/psp2cxml-tool).

```bash
export VITASDK=/path/to/vitasdk
bash build.sh
```

VPK: `build/vita_moonlight_paf.vpk`

`build.sh` compiles CXML → RCO when `psp2cxml-tool` is available. Without it the binary still links, but the UI will not match this layout.

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

## License

Part of the Vita Moonlight ecosystem. Add a LICENSE before shipping a release.
