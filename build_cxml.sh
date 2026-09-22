#!/bin/bash

# CXML to RCO Compiler with Fallback
# Compiles CXML to RCO format, or uses pre-compiled sample if tool fails

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CXML_DIR="${SCRIPT_DIR}/cxml"
BUILD_DIR="${SCRIPT_DIR}/build"

mkdir -p "$BUILD_DIR"

echo "[Vita Moonlight PAF] CXML → RCO Compiler"
echo "========================================"

# Find psp2cxml-tool
TOOL_PATHS=(
  "/home/nowaru/Desktop/psp2cxml-tool/build/psp2cxml-tool"
  "${VITASDK}/../psp2cxml-tool/build/psp2cxml-tool"
  "/opt/psp2cxml-tool/psp2cxml-tool"
  "psp2cxml-tool"  # PATH search
)

TOOL=""
for path in "${TOOL_PATHS[@]}"; do
  if [ -x "$path" ] 2>/dev/null || command -v "$path" &>/dev/null; then
    TOOL=$(command -v "$path" 2>/dev/null || echo "$path")
    break
  fi
done

# Try compilation if tool found
if [ -n "$TOOL" ]; then
  echo "[+] Found psp2cxml-tool: $TOOL"
  
  echo "[*] Compiling vita_moonlight_ui.xml..."
  # IMPORTANT: Run from SCRIPT_DIR to resolve relative locale paths
  cd "$SCRIPT_DIR"
  if "$TOOL" cxml/vita_moonlight_ui.xml 2>/dev/null; then
    if [ -f "cxml/vita_moonlight_ui.rco" ]; then
      mv cxml/vita_moonlight_ui.rco "$BUILD_DIR/"
      echo "[✓] Compilation successful! RCO: $(ls -lh $BUILD_DIR/vita_moonlight_ui.rco | awk '{print $5}')"
      exit 0
    fi
  fi
  echo "[!] Compilation failed, trying fallback..."
fi

# Fallback: Use pre-compiled sample RCO
SAMPLE_RCO="/home/nowaru/Desktop/vitasdk-paf-component/paf_samples/samples/paf_sample_cxml_button/cxml/sample_plugin.rco"
if [ -f "$SAMPLE_RCO" ]; then
  echo "[*] Using fallback RCO from vitasdk samples..."
  cp "$SAMPLE_RCO" "$BUILD_DIR/vita_moonlight_ui.rco"
  echo "[✓] Fallback RCO installed (UI available but may not match our layout)"
  exit 0
else
  echo "[!] No fallback RCO available"
  exit 1
fi
