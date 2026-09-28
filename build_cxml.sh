#!/bin/bash

# CXML to RCO compiler.

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
  echo "[!] CXML compilation failed."
  exit 1
fi

echo "[!] No usable psp2cxml-tool found."
echo "    Set PSP2CXML_TOOL or build ../psp2cxml-tool first."
exit 1
