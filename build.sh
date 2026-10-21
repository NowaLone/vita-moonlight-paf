#!/bin/bash
# Build both launch profiles by default, or one explicitly selected profile.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build.sh [CMake arguments]

Without an explicit MOONLIGHT_SYSTEM_APP argument, build both normal and system
profiles in build/normal and build/system. Copy their VPK/SELF/debug ELF files
to build/vita_moonlight_paf-{normal,system}.{vpk,self,elf}.

To build a single profile in the legacy build/ directory:
  ./build.sh -DMOONLIGHT_SYSTEM_APP=OFF
  ./build.sh -DMOONLIGHT_SYSTEM_APP=ON

Heap tracing defaults OFF for each invocation. In a dual-profile build,
-DMOONLIGHT_HEAP_TRACE=ON enables tracing only in the system profile.
The idle disconnect/reconnect test defaults OFF per invocation; enable explicitly
with -DMOONLIGHT_DISCONNECT_TEST=ON for a test package. The separate active-stream
scenario uses -DMOONLIGHT_ACTIVE_DISCONNECT_TEST=ON instead; pending setup uses
-DMOONLIGHT_PENDING_DISCONNECT_TEST=ON. In-flight HTTPS uses -DMOONLIGHT_HTTP_DISCONNECT_TEST=ON.
Common-c startup uses -DMOONLIGHT_STARTUP_DISCONNECT_TEST=ON, stage 4 by default.
Add -DMOONLIGHT_STARTUP_DISCONNECT_STAGE=10 to test cancellation after video setup.
All five default OFF and are mutually exclusive.
Other CMake arguments are forwarded to the selected profile(s).
EOF
}

parse_arguments() {
    BUILD_SELECTION=both
    CMAKE_ARGS=()
    local definition value
    while (( $# )); do
        case "$1" in
            -h|--help) usage; exit 0 ;;
            -D)
                if (( $# < 2 )); then
                    echo "ERROR: -D requires a CMake definition." >&2
                    exit 1
                fi
                definition="$2"
                CMAKE_ARGS+=("$1" "$2")
                shift 2
                ;;
            -D*)
                definition="${1#-D}"
                CMAKE_ARGS+=("$1")
                shift
                ;;
            *) CMAKE_ARGS+=("$1"); shift; continue ;;
        esac
        if [[ "$definition" =~ ^MOONLIGHT_SYSTEM_APP(:[^=]+)?=(.*)$ ]]; then
            value="${BASH_REMATCH[2]^^}"
            # Match CMake's false constants; all other values enable the option.
            case "$value" in
                ''|0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|*-NOTFOUND) BUILD_SELECTION=normal ;;
                *) BUILD_SELECTION=system ;;
            esac
        fi
    done
}

build_profile() {
    local profile="$1" build_dir="$2" mode=OFF
    [[ "$profile" == system ]] && mode=ON
    local -a profile_args=(-DMOONLIGHT_HEAP_TRACE=OFF -DMOONLIGHT_DISCONNECT_TEST=OFF
        -DMOONLIGHT_ACTIVE_DISCONNECT_TEST=OFF -DMOONLIGHT_PENDING_DISCONNECT_TEST=OFF
        -DMOONLIGHT_HTTP_DISCONNECT_TEST=OFF -DMOONLIGHT_STARTUP_DISCONNECT_TEST=OFF
        -DMOONLIGHT_STARTUP_DISCONNECT_STAGE=4 "${CMAKE_ARGS[@]}"
        "-DMOONLIGHT_SYSTEM_APP=$mode")
    if [[ "$BUILD_SELECTION" == both && "$profile" == normal ]]; then
        profile_args+=(-DMOONLIGHT_HEAP_TRACE=OFF)
    fi

    mkdir -p "$build_dir"
    if [[ "$build_dir" != "$BUILD_ROOT" ]]; then
        cp "$BUILD_ROOT/vita_moonlight_ui.rco" "$build_dir/vita_moonlight_ui.rco"
    fi
    echo "[*] Building $profile profile in $build_dir"
    (
        cd "$build_dir"
        cmake -DCMAKE_BUILD_TYPE=Release "${profile_args[@]}" "$PROJECT_DIR"
        make -j"$BUILD_JOBS"
    )
}

build_selected_profiles() {
    local profile build_dir extension
    local -a profiles=(normal system)
    if [[ "$BUILD_SELECTION" != both ]]; then
        profiles=("$BUILD_SELECTION")
    fi
    for profile in "${profiles[@]}"; do
        build_dir="$BUILD_ROOT"
        [[ "$BUILD_SELECTION" == both ]] && build_dir="$BUILD_ROOT/$profile"
        build_profile "$profile" "$build_dir"
    done

    # Publish named results only after all requested profiles finish successfully.
    for profile in "${profiles[@]}"; do
        build_dir="$BUILD_ROOT"
        [[ "$BUILD_SELECTION" == both ]] && build_dir="$BUILD_ROOT/$profile"
        for extension in vpk self elf; do
            cp "$build_dir/vita_moonlight_paf.$extension" \
                "$BUILD_ROOT/vita_moonlight_paf-$profile.$extension"
        done
        echo "[+] $profile VPK: $BUILD_ROOT/vita_moonlight_paf-$profile.vpk"
        echo "    Debug ELF: $BUILD_ROOT/vita_moonlight_paf-$profile.elf"
        echo "    SELF: $BUILD_ROOT/vita_moonlight_paf-$profile.self"
    done
}

parse_arguments "$@"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="$PROJECT_DIR/build"
export VITASDK="${VITASDK:-/usr/local/vitasdk}"
BUILD_JOBS="$(nproc 2>/dev/null || echo 1)"

echo "[Vita Moonlight PAF UI] Build Script"
echo "VITASDK: $VITASDK"
echo "Project: $PROJECT_DIR"
echo "Profiles: $BUILD_SELECTION"

if [[ ! -d "$VITASDK" ]]; then
    echo "ERROR: VITASDK not found at: $VITASDK" >&2
    echo "Set VITASDK to your SDK path." >&2
    exit 1
fi

# CXML is identical for both profiles; compile once and copy to each build tree.
mkdir -p "$BUILD_ROOT"
bash "$PROJECT_DIR/build_cxml.sh"
build_selected_profiles

echo "[+] All requested profiles built successfully."
echo "Install the selected VPK through VitaShell. Both profiles use title ID VLMP00001."
