#!/bin/bash
# Full development rebuild + PS Vita deploy for vita-moonlight-paf.
#
# Requirements:
#   - VITASDK
#   - ../psp2cxml-tool (or PSP2CXML_TOOL)
#   - psp2shell_cli
#   - psp2shell plugins installed on the Vita
#
# Usage:
#   ./dev-build-run.sh
#   PSVITAIP=192.168.1.123 ./dev-build-run.sh
#
# The target app must already be installed on the Vita once (VPK install).
# After that, psp2shell "load" replaces its eboot and restarts it.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
TITLE_ID="VLMP00001"
SELF_FILE="$BUILD_DIR/vita_moonlight_paf.self"
IP_FILE="$ROOT/psvitaip.txt"
PSP2SHELL_CLI="${PSP2SHELL_CLI:-psp2shell_cli}"

log() {
    echo
    echo "[*] $1"
}

fail() {
    echo
    echo "[!] ERROR: $1" >&2
    exit 1
}

# Resolve Vita IP from environment first, then psvitaip.txt.
VITA_IP="${PSVITAIP:-}"
if [ -z "$VITA_IP" ] && [ -f "$IP_FILE" ]; then
    VITA_IP="$(grep -v '^[[:space:]]*#' "$IP_FILE" | head -n 1 | tr -d '[:space:]')"
fi

[ -n "$VITA_IP" ] || fail "Vita IP is not set. Use PSVITAIP=... or create $IP_FILE."

# Resolve psp2shell_cli either as a command or an explicit path.
if [[ "$PSP2SHELL_CLI" == */* ]]; then
    [ -x "$PSP2SHELL_CLI" ] || fail "psp2shell_cli is not executable: $PSP2SHELL_CLI"
else
    command -v "$PSP2SHELL_CLI" >/dev/null 2>&1 ||
        fail "psp2shell_cli not found in PATH. Set PSP2SHELL_CLI=/path/to/psp2shell_cli."
fi

log "Cleaning build directory"
rm -rf "$BUILD_DIR"

log "Running build.sh"
cd "$ROOT"
bash "$ROOT/build.sh"

[ -f "$SELF_FILE" ] || fail "SELF was not produced: $SELF_FILE"

log "Deploying $SELF_FILE to Vita $VITA_IP"
echo "[*] psp2shell_cli $VITA_IP 3333 load $TITLE_ID $SELF_FILE"

"$PSP2SHELL_CLI" "$VITA_IP" 3333 load "$TITLE_ID" "$SELF_FILE"

echo
echo "======================================="
echo "[+] Build and deploy completed!"
echo "======================================="
echo
echo "Title ID: $TITLE_ID"
echo "Vita IP:  $VITA_IP"
echo "SELF:     $SELF_FILE"
