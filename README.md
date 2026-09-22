# Vita Moonlight PAF

Empty native PS Vita shell for a future [vita-moonlight](https://github.com/xyzz/vita-moonlight) UI.

It uses PAF so the app looks like a system Vita program. There is no streaming, pairing, or host discovery yet — only navigation and layout.

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
| Lower right balloon | `300, -150` |

`list_view` height must be a multiple of 32.

## Navigation

Pages are stacked. `page_main` stays at the bottom. Back / the bottom-left corner button closes the top overlay. Tapping outside the settings balloon dismisses it.

## Future moonlight hook

Keep UI-only code in `src/paf_sample.cpp`. Streaming should come in later through a small C API (search, add host, start stream) called from the existing button callbacks — do not fold `libgamestream` into the PAF plugin directly.

## License

Part of the Vita Moonlight ecosystem. Add a LICENSE before shipping a release.
