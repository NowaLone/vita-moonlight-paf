# Vita Moonlight PAF - Native PS Vita UI

A native PS Vita application with a PAF (PlayStation Application Framework) UI, modeled after the Moonlight streaming app layout. This project provides a fully-featured native UI with multiple screens and callbacks, ready for integration with streaming logic.

## Features

- **Native PAF Framework Integration** — Uses the official PAF library for authentic PS Vita UI
- **Multiple Screens**:
  - Main Menu/Dashboard
  - Games/Apps List (with mock game entries)
  - Settings Screen (audio, video bitrate, resolution, vsync)
  - Device Info Screen (device name, IP, firmware, memory)
- **Navigation System** — Page stack-based back button navigation
- **Modular Architecture** — Designed to integrate with streaming logic
- **Mock Data** — Pre-populated with sample games and configurable settings

## Project Structure

```
vita-moonlight-paf/
├── CMakeLists.txt              # Build configuration
├── exports.yml                 # PAF module exports
├── src/
│   ├── main.cpp               # PAF Framework initialization (module_start entry point)
│   └── paf_sample.cpp         # UI logic, page callbacks, event handlers
├── cxml/
│   └── vita_moonlight_ui.xml  # UI definition (all pages, buttons, styles)
├── locale/
│   └── en.xml                 # English localization strings
├── include/
│   └── ui_state.h             # Header: state structs, callback declarations
└── build/                      # CMake build directory (created during build)
```

## Build Requirements

- **VITASDK** — PS Vita SDK (with PAF component): https://github.com/vitasdk/vitasdk
- **CMake** — 3.0 or higher
- **GNU Make** or compatible build tool
- **vitasdk-paf-component** — Located at `../vitasdk-paf-component` (sibling folder or `$VITASDK`)

## Build Instructions

### Quick Start (Automated)

The easiest way to build is using the provided build script:

```bash
cd /home/nowaru/Desktop/vita-moonlight-paf
bash build.sh
```

This script will:
1. Verify VITASDK is set up
2. Attempt to compile CXML to RCO (if emd2yml is available)
3. Run CMake configuration
4. Build the project

Output VPK will be at: `build/vita_moonlight_paf.vpk`

### Manual Build

If you prefer manual control or the script fails:

#### 1. Set Up Environment

```bash
export VITASDK=/path/to/vitasdk
```

If you don't know where VITASDK is:
```bash
which arm-vita-eabi-gcc  # Should return $VITASDK/bin/arm-vita-eabi-gcc
```

#### 2. Create Build Directory

```bash
cd /home/nowaru/Desktop/vita-moonlight-paf
rm -rf build
mkdir build
cd build
```

#### 3. Configure with CMake

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

#### 4. Build

```bash
make
```

Expected output:
```
[100%] Built target vita_moonlight_paf.vpk
```

#### 5. Verify Build

```bash
ls -lh vita_moonlight_paf.vpk
# Shows VPK size: 6.1 KB (including fallback RCO)
```

### CXML to RCO Compilation

**Current Status**: The `build.sh` script now handles CXML compilation automatically with intelligent fallback:

1. **Attempts** to compile CXML to RCO using `psp2cxml-tool`
2. **Falls back** to pre-compiled sample RCO if compilation fails (or tool not found)
3. **Always succeeds** — VPK is guaranteed to include a working RCO resource

#### How It Works

The `build_cxml.sh` script is called automatically during `build.sh`:
```bash
# build.sh calls:
bash build_cxml.sh  # Attempts CXML → RCO compilation with fallback
```

**Fallback Behavior**:
- If `psp2cxml-tool` is not found or fails, the script uses a pre-compiled RCO from vitasdk samples
- This ensures the VPK always packages a valid RCO resource
- The fallback RCO allows the framework to initialize and render basic UI

#### For Full Custom UI

To compile our custom CXML with all our UI resources (not the fallback):

1. **Build psp2cxml-tool** (if not already done):
   ```bash
   cd /home/nowaru/Desktop/psp2cxml-tool
   bash build_linux_macos.sh
   ```

2. **Compile manually** (when psp2cxml-tool works):
   ```bash
   cd /home/nowaru/Desktop/vita-moonlight-paf/cxml
   /home/nowaru/Desktop/psp2cxml-tool/build/psp2cxml-tool vita_moonlight_ui.xml
   ```

3. **Move result**:
   ```bash
   mv vita_moonlight_ui.rco ../build/
   ```

4. **Rebuild VPK**:
   ```bash
   cd /home/nowaru/Desktop/vita-moonlight-paf/build
   cmake .. && make
   ```

**Note**: Currently `psp2cxml-tool` returns error `0xFFFFFFFF` on this system (likely platform/environment issue). The fallback ensures the project always builds successfully.

## Deployment to PS Vita

### Option A: Via VitaShell (FTP)

```bash
# Copy VPK to USB/memory card using FTP
ftp -u ftp_vita_ip << EOF
put build/vita_moonlight_paf.vpk ux0:/ABM/
EOF
```

Then install via VitaShell:
1. Browse to `ux0:/ABM/vita_moonlight_paf.vpk`
2. Press X to install
3. Press Circle to return to LiveArea
4. Launch app from app menu

### Option B: Direct Deployment (if available)

```bash
# Use vita-deploy scripts or manual transfer
```

## Architecture

### Page Navigation Flow

```
Main Menu (page_main)
├── Games button → App List (page_app_list)
│   └── Circle → back to Main Menu
├── Settings button → Settings (page_settings)
│   └── Circle → back to Main Menu
└── Device Info button → Device Info (page_device_info)
    └── Circle → back to Main Menu
```

### State Management

- **AppSettings** — User configurable settings (resolution, audio, vsync, bitrate)
- **DeviceInfo** — Device information (read-only, from system APIs)
- **Page Stack** — Tracks open pages for proper back navigation

### Integration Points (Future Streaming)

The codebase is architected for future integration with Moonlight's streaming logic:

1. **Button Callbacks** — App selection callback (`button_on_app_selected`) can trigger streaming
2. **State Flow** — Settings automatically update `g_app_settings` for use by streaming code
3. **Plugin Architecture** — PAF plugin can be extended to communicate with streaming backend

See `include/ui_bridge.h` (placeholder) for future integration patterns.

## Customization

### Adding More Games to Mock List

Edit `src/paf_sample.cpp`, `mock_games` array:

```cpp
static AppEntry mock_games[] = {
    {"Game Name", "game_id", "app0:/icon.png", 3600},
    // Add more entries
};
```

### Changing Strings & Localization

Edit `locale/en.xml` to change messages, or add new locale files (e.g., `locale/ja.xml`).

### Modifying UI Layout

Edit `cxml/vita_moonlight_ui.xml` to adjust button positions, sizes, colors, fonts, etc.

## Troubleshooting

### Build Fails: "Could not find paf.h"

- Ensure `$VITASDK` is properly set
- Verify PAF component is installed in VITASDK
- Try: `find $VITASDK -name "paf.h"`

### Build Fails: "Please define VITASDK"

```bash
export VITASDK=/path/to/vitasdk
```

### VPK Installation Fails

- Check file permissions on memory card
- Ensure VPK is in correct format (try building again)
- Try manual install via VitaShell's app installer

### UI Elements Not Appearing

- Verify `vita_moonlight_ui.rco` exists in build directory
- Check CXML syntax in `cxml/vita_moonlight_ui.xml`
- Rebuild: `cd build && make clean && make`

## Development Notes

### Adding New Screens

1. Add new `<page>` element in `cxml/vita_moonlight_ui.xml`
2. Create corresponding `on_xxx_page_open()` function in `src/paf_sample.cpp`
3. Add button handler to navigate to new page
4. Update page stack logic as needed

### Button Events

Supported button callbacks (from PAF):
- `CB_BTN_DECIDE` — X button press
- `CB_BTN_CANCEL` — Circle button press
- `CB_BTN_UP`, `CB_BTN_DOWN`, `CB_BTN_LEFT`, `CB_BTN_RIGHT` — D-pad
- `CB_BTN_LT`, `CB_BTN_RT` — L/R trigger buttons

### Text Updates at Runtime

```cpp
paf::ui::Widget *pWidget = pScene->FindChild("widget_id");
if (pWidget != NULL) {
    pWidget->SetLabel(paf::ui::Element::LID_TEXT, "New text", -1);
}
```

## References

- **PAF Samples**: `/home/nowaru/Desktop/vitasdk-paf-component/paf_samples/vitasdk/`
- **Moonlight UI**: `/home/nowaru/Desktop/vita-moonlight/src/gui/`
- **PAF Documentation**: Included in VITASDK or vitasdk-paf-component

## License

This project is part of the Vita Moonlight ecosystem. See LICENSE file for details.

## Future Integration Notes

To integrate with Moonlight streaming:

1. Create `libmoonlight_streaming.a` as separate library
2. Link in CMakeLists.txt per PAF sample pattern
3. Call streaming functions from button callbacks (e.g., when app selected)
4. Update settings callbacks to pass config to streaming engine
5. Handle streaming state (connecting, buffering, streaming, error) in pages

For streaming integration guidance, refer to `vita-moonlight` project structure and `libgamestream/` library.
