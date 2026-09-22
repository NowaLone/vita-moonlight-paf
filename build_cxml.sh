#!/bin/bash

# CXML to RCO compiler with optional sample fallback.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

mkdir -p "$BUILD_DIR"

echo "[Vita Moonlight PAF] CXML → RCO Compiler"
echo "========================================"

TOOL_PATHS=(
  "${PSP2CXML_TOOL:-}"
  "${SCRIPT_DIR}/../psp2cxml-tool/build/psp2cxml-tool"
  "${VITASDK}/../psp2cxml-tool/build/psp2cxml-tool"
  "/opt/psp2cxml-tool/psp2cxml-tool"
  "psp2cxml-tool"
)

TOOL=""
for path in "${TOOL_PATHS[@]}"; do
  if [ -z "$path" ]; then
    continue
  fi
  if [ -x "$path" ] || command -v "$path" >/dev/null 2>&1; then
    TOOL=$(command -v "$path" 2>/dev/null || echo "$path")
    break
  fi
done

if [ -n "$TOOL" ]; then
  echo "[+] Found psp2cxml-tool: $TOOL"
  echo "[*] Compiling vita_moonlight_ui.xml..."
  cd "$SCRIPT_DIR"
  if "$TOOL" cxml/vita_moonlight_ui.xml; then
    if [ -f "cxml/vita_moonlight_ui.rco" ]; then
      mv cxml/vita_moonlight_ui.rco "$BUILD_DIR/"
      echo "[✓] Compilation successful"
      exit 0
    fi
  fi
  echo "[!] Compilation failed, trying fallback..."
fi

SAMPLE_RCO="${VITASDK}/../vitasdk-paf-component/paf_samples/samples/paf_sample_cxml_button/cxml/sample_plugin.rco"
if [ -f "$SAMPLE_RCO" ]; then
  echo "[!] Using sample fallback RCO — custom layout will NOT appear"
  cp "$SAMPLE_RCO" "$BUILD_DIR/vita_moonlight_ui.rco"
  exit 0
fi

echo "[!] No RCO produced. Install psp2cxml-tool and retry."
exit 1
