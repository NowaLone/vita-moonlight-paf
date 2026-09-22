#!/bin/bash
# Complete build script for Vita Moonlight PAF UI
# This script handles CXML compilation and CMake build in one shot

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
VITASDK="${VITASDK:=/usr/local/vitasdk}"

echo "[Vita Moonlight PAF UI] Build Script"
echo "===================================="
echo ""
echo "VITASDK: $VITASDK"
echo "Project: $PROJECT_DIR"
echo ""

# Verify VITASDK exists
if [ ! -d "$VITASDK" ]; then
    echo "ERROR: VITASDK not found at: $VITASDK"
    echo "Please set VITASDK environment variable:"
    echo "  export VITASDK=/path/to/vitasdk"
    exit 1
fi

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo "[*] Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Try to compile CXML (optional for now, with graceful fallback)
echo "[*] Attempting to compile CXML to RCO..."
if bash "$PROJECT_DIR/build_cxml.sh" 2>/dev/null; then
    echo "[+] CXML compiled successfully"
else
    echo "[!] CXML compilation skipped (psp2cxml-tool not available)"
    echo "    UI will not display, but binary will build"
    echo "    See build_cxml.sh for setup instructions"
fi

echo ""
echo "[*] Configuring CMake..."
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release "$PROJECT_DIR"

echo "[*] Building project..."
make -j$(nproc 2>/dev/null || echo 1)

echo ""
echo "=================================="
echo "[+] Build completed successfully!"
echo "=================================="
echo ""
echo "Output files:"
echo "  - Executable: $BUILD_DIR/vita_moonlight_paf"
echo "  - SELF: $BUILD_DIR/vita_moonlight_paf.self"
echo "  - VPK:  $BUILD_DIR/vita_moonlight_paf.vpk"
echo ""
echo "Next steps:"
echo "  1. Deploy VPK to PS Vita (see README.md)"
echo "  2. To fix RCO compilation, ensure psp2cxml-tool is built:"
echo "     cd $PROJECT_DIR/../psp2cxml-tool && bash build_linux_macos.sh"
echo "  3. Then compile CXML: bash build_cxml.sh"
echo "  4. Finally rebuild: cd build && cmake .. && make"
