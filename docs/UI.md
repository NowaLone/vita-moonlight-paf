# UI guide

How the Vita Moonlight PAF interface is built, which rules keep it looking like a native Vita app, and the
measured numbers behind every layout decision. Read this before changing anything under `cxml/`, `src/pages/`,
`src/option_menu.cpp` or `include/pages/`.

Companion documents: `AGENTS.md` (hard rules for agents), `ROADMAP.md` (plan and hardware checks), `README.md`
(overview and build).

Evidence tags used below:

- **[hw]** verified on a real Vita.
- **[shot]** measured from a Vita screenshot (a reference app or this app); not re-checked on hardware.
- **[code]** read from the source; not a claim about how it looks.
- **[open]** not verified; do not treat as fact.

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
- `cxml/vita_moonlight_ui.xml` contains a stray line `ge>` between `</page>` and `</pagetable>` from an earlier edit.
  It is parsed as harmless text; remove it when the file is next touched.
- `cxml/locale/en.xml` contains raw `&` and `<->` in some attributes. The project's tool accepts them; a strict XML
  parser does not.

## 3. Pages

| Class (`page::Type`) | XML page | Purpose | Back mechanism |
|---|---|---|---|
| `Main` (`Type_Main`) | `page_main` | saved-PC list, inline Add PC / Search PCs, selection mode | none (bottom of the stack) |
| `Search` (`Type_Search`) | `page_search_pcs` | LAN discovery list; the header text is the status | corner button `btn_close_search` |
| `Pairing` (`Type_Pairing`) | `page_pairing` | shows the PIN, pairs as soon as it opens | bar button `btn_close_pairing` |
| `Apps` (`Type_Apps`) | `page_apps` | application list of the connected PC | corner button `btn_close_apps` |
| `Stream` (`Type_Stream`) | `page_stream` | full-screen video plane, black background, FPS text; no chrome | none (closed by stream events) |
| `OptionMenu` (`Type_OptionMenu`) | `page_settings_bubble` | speech-balloon overlay: Delete, Settings | dismiss button over the whole screen |

Navigation **[code]**: Main -> `OptionMenu` -> (Settings = the system AppSettings plugin | Delete = selection mode on
Main). Main -> Search -> connect -> Pairing (unpaired host) or Apps (paired host). Main -> Add PC (inline button,
`SceImeDialog`) -> connect -> Pairing or Apps. Pairing -> Apps. Apps -> Stream.

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
`style_text_flow_note` (22, 85% white, wrapped), `style_text_main_status` (22, centred; used for page status text and the
stream FPS overlay).

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

- Setup per page: `SetItemFactory(new ListViewFactory(this))`, `InsertSegment(0, 1)`, `SetCellSizeDefault(0, {960, 80})`,
  `SetSegmentLayoutType(0, LAYOUT_TYPE_LIST)`. The factory opens a template into `param.parent` and takes the last child
  as the item.
- Refresh: `DeleteCell(0, 0, existing)` then `InsertCell(0, 0, count)`. **The third argument of `DeleteCell` is a
  count, not the last index** **[hw on Main]**; `existing - 1` leaves a stale cell behind (a duplicate row). Search and
  Apps use the same call; their hardware check is open.
- Cells are created lazily by the factory; do not keep widget pointers across a refresh. Row arrays hold at most 8
  entries.
- Selection mode rebuilds the cells and picks `template_host_list_item_select` instead of `template_host_list_item`.
  The checkbox exists only in the select template.

### 4.8 Status text

- Main, Apps and Pairing show status as centred or left text on the list area; Search uses the header text itself as
  the status.
- The idle hint is hidden while rows are listed, because the text is centred on the list and would be drawn over
  row two **[shot]**. On Main every return to the idle state goes through `Main::ShowIdleStatus()`, never a direct
  `SetStatus("Select a PC to connect.")`; Apps hides it in `ShowApps()`. Transient messages (connecting, launching,
  errors) are still drawn over the rows.
- `SetStatus()` shows the widget; selection mode keeps the Main status hidden.

### 4.9 Events and threads **[code]**

Backend callbacks may arrive on worker threads. `MoonlightApp` queues them and delivers them through
`MainThreadCallList`, so page callbacks run on the PAF main thread. Only the topmost page owns the event callback; when
a page is destroyed it hands the callback back to the page below (`RestoreEventCallback()` in `Main`, `Search`, `Apps`,
`Pairing`). Streaming stays behind the `moonlight_api_*` boundary; never move it into a page.

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
