# Settings header and Wi-Fi indicators

Read-only resource/ELF investigation, 2026-10-23. This supplements the screenshot
measurements in [UI.md](UI.md); it does not change Moonlight's interface. **[code]**
below means decoded RCO or inspected ARM instructions, not native execution.

## Supplied files and method

The maintainer supplied these firmware files; their exact firmware version was not
established. Do not generalize the hashes/layout to every firmware:

- `system_settings_main.rco`, SHA-256
  `b5dcc4cd3f7c3c0705989dacc4bf5668c7038418a918a483f80de038bc9b02bf`.
- `system_settings_plugin.rco`, SHA-256
  `1a460c4ea1b58d38d3289b96211a6c88ec7da56495a84f6a1b3dddff4bdddd4e`.
- Settings `eboot.bin.elf`, SHA-256
  `e1a80dfa535e6d5b6eed80ef3fb559bd099cec3fbc1152e93a2adcf49913f788`.
- `wifi_settings_plugin.rco`, SHA-256
  `564c0ba09f9ecdf7fd7bde1005ff481997f9c8cebfa1709e0a8f3e016dea0f37`.
- Previously supplied `common_resource.rco`, SHA-256
  `6ee58e58d1e78fecd99ea93e585c81efde3e940e3e0ca5cec4e64ba7cacdc806`.
- Screenshot `2026-10-23-022829.jpg`, 960 x 544.

RCO tree/attribute/file tables were decoded without compiling resources. Compressed
payloads were decompressed with zlib; GIM palette pixels and alpha were exported to
PNG. Main/plugin/Wi-Fi contain respectively 221/40/265 nodes and 17/17/18 textures.
Unknown names below are written as `hash_XXXXXXXX`; those are descriptive labels
for original ID hashes, not recovered Sony resource names. RCO node offsets are
relative to the tree, whose file offset is `0x50` in these files.

The ELF identifies itself as `SceSettings`. Its first LOAD maps file `0xe0` to
`0x81000000` (size `0x2938dc`); its second LOAD maps file `0x293a00` to
`0x81294000`. Code addresses below are virtual addresses in this supplied image.
Import NIDs were matched against the installed VitaSDK/PAF databases; unresolved
imports retain numeric NIDs. No builds, module execution or hardware tests.

## Header: the exact resource is local to Settings

**[code]** `system_settings_main.rco` has this complete header composition:

```text
template hash_b047ffd3                         tree 0x23c
  plane hash_bebf0207                          tree 0x278
    style _common_style_plane_transparent
    align 0,2; size 960,64; pos 0,240
    box hash_d2290b7b                          tree 0x310
      horizontal layout; adjust 2,2; centred
      text main_page_title_text               tree 0x3d8
        style style_text_header
        size 760,0; align 0,4; adjust 0,2
    plane hash_966f95fd                        tree 0x480
      style hash_8d082ccd
      size 960,2; pos 0,0; anchor 0,1
  button hash_8211f03f                         sibling of header plane
    size 46,46; pos 32,208; anchor 1,0
```

`style_text_header` (tree `0x24bc`) explicitly binds its inner `textobj`:
32 px, bold, opaque white, centred X/Y, ellipsis enabled. The box can adjust to
content; the plain header text slot is 760 px wide. This is an ordinary PAF
plane/box/text composition supplied to the Settings UI, not evidence of a separate
PAF widget class called a system title bar. The sibling back-style button exists
in the template; its visibility on a particular page depends on runtime code.

The separator chain is **[code]**:

```text
plane hash_966f95fd
  -> style_plane hash_8d082ccd                 tree 0x2444
  -> planeobj.texture0 = hash_69cb3790
  -> texture hash_69cb3790                     tree 0x38c4
  -> compressed payload file 0x5530, length 0x322
```

The payload is a 960 x 2 indexed GIM, 3152 bytes after decompression. Its SHA-256 is
`de801d8a31d34014730390cf6250a39a05b9e1ba2a52e7dbf14222a52009f7e1`.
There is no `div_mode` or margin metadata on this texture. Row 0 is black, maximum
alpha 68/255 (26.7%); row 1 is white, maximum alpha 52/255 (20.4%). It fades toward
the sides; nonzero-alpha bounds are `(36,0)-(922,2)`.

This is **not** `_common_texture_ruled_line`. The supplied common texture is
328 x 4, with transparent outer rows, `div_mode="3x1"`, `div_ratio_h≈0.49`, and
maximum black/white alpha 90/18. The former project `header_line.png` was also
960 x 2, but had alpha maxima 51/46; decoded pixels are not identical to Settings.
The 150 x 4 Wi-Fi-local line (`hash_0cec900a`, margin 1/1, 912 x 2 template) is
another separator and must not be confused with this header asset.

**[code]** The ELF loads `vs0:app/NPXS10015/system_settings_main.rco` through
`paf::Plugin::LoadAsync` at `0x810001fa` (path assembled at `0x81000190`). Wi-Fi
page handling accesses the shared `main_page_title_text` at `0x8112081c` and calls
`Widget::FindChild` at `0x81120862`; this path replaces the title with the saved
AP name for `page_wifi_saved_ap_menu`. No direct reference to the header template's
numeric hash was identified in the ELF; the exact shared framework constructor
path was not fully reconstructed. The RCO dependency chain itself is explicit.

**[shot]** The screenshot has the system infobar above y≈32, a header band through
y≈96, and dark/light separator rows around y=95/96. Bright title pixels span
approximately x=370..588, y=49..82, including the descender and JPEG edges.

## Wi-Fi dot: connection state, with an animated intermediate state

**[code]** The Wi-Fi plugin owns two 30 x 30 textures used by this code path:

- `hash_fbd879ae` (texture node `0x5144`): bright green sphere/glow.
- `hash_9718c305` (texture node `0x51b0`): dim olive sphere with a bright upper half.

`hash_a6c087bd` is a translucent ring texture in the same file. Its presence does
not establish a third AP status: the traced Wi-Fi paths below switch the two sphere
textures. An ELF use of the ring at `0x81019dca` is in Bluetooth-plugin handling,
so a shared hash alone must not be assigned Wi-Fi semantics without plugin context.

The Wi-Fi template `hash_e454df4f` (tree `0x1808`) creates a `button`
`hash_c3232534` (tree `0x1834`), size 27 x 27, pos -302,0. Its style
`hash_1401d780` (tree `0x2a38`) binds the green texture as `glow_obj`.
The executable also creates a plain 27 x 27 PAF `Plane` dynamically. Runtime names
are `icon_blink_button` and `icon_blink_plane`; these names are ELF strings,
not invented names for the RCO IDs.

In the row-rendering routine beginning at `0x81124dfc`, the AP entry's word at
offset `0xb8` selects the presentation (`0x81125944`):

- States 2, 3 or 5 enter the animated branch (`0x81125948..0x81125952`). It uses
  the dim base texture (`GetTexture` at `0x81125d80`), opens the button template
  (`0x81125dfe`), obtains the green texture (`0x8112601a`) and calls
  `Widget::SetLoopEffect(7, true)` (`0x81126052`).
- State 4 enters the steady branch (`0x81126060`), uses the green texture
  (`GetTexture` at `0x81126480`) for the plain indicator, and removes the separate
  blink-button widget when present.
- Other states use the dim texture (`GetTexture` at `0x8112675e`) and remove the
  blink-button path when present.

A second indicator-rendering path contains the same state-4 test at `0x8112a3d6`,
green texture lookup at `0x8112a5d0`, dim lookup at `0x8112a650`, and animated
`SetLoopEffect(7,true)` at `0x8112a3d0`. Both paths pass the retrieved surface to
virtual slot `0x100`, matching `Widget::SetTexture(surface)` in the installed PAF
header. The exact timing/visual waveform of loop effect 7 was not measured; do not
invent a blink frequency from its numeric value.

The state is not taken from row focus. The status updater obtains it through
`0x8112d1fe -> 0x811401ac -> SceNetCtl NID 0xe8e8df18`, then writes that value to
the matching AP entry at offset `0xb8` (`0x81137d7a`). The connecting branch
(`0x81137bd8..0x81137be8`) tests 2/3/5 and selects message hash `0x500e7607`;
the connected branch selects `0xda7e42d0` (`0x81137cee..0x81137cfa`). Decoding
the English locale embedded in the Wi-Fi RCO gives respectively:

- `0x500e7607`: `Connecting... (%1)`.
- `0xda7e42d0`: `Connected. (%1)`.

The preceding branch bounds relevant active AP states to 2/3/4/5
(`0x811378f6..0x81137906`), so the connected alternative is state 4. This confirms
the interpretation: animated green during connection, steady green when connected,
dim otherwise. It does not mean that dim APs are unreachable. Individual names of
the intermediate states and the private NetCtl import remain unresolved.

**[shot]** Dot centres are approximately x=178, y=327/397/467/537, with 70 px row
pitch and title origin x≈208. This matches the 27 px slot and -302 position in a
960 px parent. The screenshot's OpenWrt is green and the summary says it is connected.

## Consequences for Moonlight

Main/Apps already have a comparable PAF plane/text/separator composition, but use
26 px regular left-aligned text at x=80, without the centred adjusting box. Search
has an independent separator and a conditional empty-list label at x=64, rather
than a persistent title. Pairing and Stream deliberately lack this standard header.
Keep these differences explicit; resource reuse alone does not make every page's
header identical to Settings.

The previous replacement of the project's PNG with `_common_texture_ruled_line`
reuses a native PAF resource, but does **not** reproduce the exact Settings header
texture. No reversal or further appearance change is made by this investigation.

The sphere artwork can be reused for a PC availability indicator, but availability
is a separate product meaning. Saved-host `fill_host()` currently sets `online=0`;
mDNS discovery sets `online=1` on arrival, without expiring results during that
scan. Wiring those fields directly would make all saved PCs dim regardless of
actual reachability. A real availability feature needs backend/service-owned
observations and main-thread UI delivery, preserving the existing row/checkbox
interaction. Do not derive availability from pairing or run network calls in the
row factory. Artwork extraction, state-branch inspection and XML checks do not
validate such a feature; no PC indicator has been implemented by this research.
