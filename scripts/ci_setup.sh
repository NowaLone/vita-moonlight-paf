#!/bin/bash
# Fresh-runner setup. This script builds dependencies; the maintainer runs it in CI.
set -euo pipefail

lock_value() {
    python3 - "$project_dir/docs/build-archives.json" "$@" <<'PY'
import json, sys
value = json.load(open(sys.argv[1]))
for key in sys.argv[2:]:
    value = value[key]
print('\n'.join(value) if isinstance(value, list) else value)
PY
}

verify_archive() {
    printf '%s  %s\n' "$1" "$2" | sha256sum --check -
}

checkout_repository() {
    git clone --no-checkout "$1" "$3"
    git -C "$3" checkout --detach "$2"
}

pin_openssl_recipe() {
    python3 - "$1" "$2" <<'PY'
import pathlib, re, sys
path, commit = pathlib.Path(sys.argv[1]), sys.argv[2]
source = 'git+https://github.com/d3m3vilurr/vita-openssl.git'
text = path.read_text()
if not re.fullmatch(r'[0-9a-f]{40}', commit) or text.count('"' + source + '"') != 1:
    raise SystemExit('Unexpected OpenSSL source recipe or invalid commit; refusing to build')
path.write_text(text.replace('"' + source + '"', '"' + source + '#commit=' + commit + '"'))
PY
}

# Setup starts here. Keep the helpers above independently checkable without building.
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_dir="${RUNNER_TEMP:?}/moonlight-deps"
: "${CI_LOG_DIR:?}" "${GITHUB_PATH:?}" "${VITASDK:?}"
if [[ "$VITASDK" != "$deps_dir/vitasdk" ]]; then
    echo 'VITASDK must point at the fresh runner dependency directory.' >&2
    exit 1
fi
# Refuse to reuse an existing SDK/cache or overwrite a working installation.
mkdir "$deps_dir"
mkdir -p "$CI_LOG_DIR/portlib-packages"
cp "$project_dir/docs/build-archives.json" "$CI_LOG_DIR/"

sdk_archive="$deps_dir/$(lock_value sdk filename)"
curl --fail --location --retry 3 --output "$sdk_archive" "$(lock_value sdk download_url)"
verify_archive "$(lock_value sdk sha256)" "$sdk_archive"
tar -xjf "$sdk_archive" -C "$deps_dir"
export PATH="$VITASDK/bin:$PATH"

packages="$deps_dir/packages"
checkout_repository "$(lock_value source_rebuild_candidate recipe_repository)" \
    "$(lock_value source_rebuild_candidate recipe_commit)" "$packages"
pin_openssl_recipe "$packages/openssl/VITABUILD" \
    "$(lock_value source_rebuild_candidate recipes openssl required_source_commit)"
cp "$packages/openssl/VITABUILD" "$CI_LOG_DIR/openssl.VITABUILD"
package_order="$(lock_value source_rebuild_candidate build_order)"
for package in $package_order; do
    (cd "$packages" && bash build.sh "$package")
    cp "$packages/$package/"*-arm.tar.xz "$CI_LOG_DIR/portlib-packages/"
done
(cd "$CI_LOG_DIR/portlib-packages" && sha256sum ./*.tar.xz) > "$CI_LOG_DIR/portlib-packages.sha256"

paf="$deps_dir/vitasdk-paf-component"
checkout_repository "$(lock_value paf_component repository)" "$(lock_value paf_component commit)" "$paf"
verify_archive "$(lock_value paf_component patch_sha256)" "$paf/vitasdk_2.patch"
(cd "$paf" && bash -e install.sh)
test "$(git -C "$paf/build/ScePaf-RE" rev-parse HEAD)" = "$(lock_value paf_component re_base_commit)"
test -f "$VITASDK/arm-vita-eabi/include/psp2/paf.h"
for library in ScePaf_stub_weak SceAppSettings_stub SceCommonGuiDialog_stub; do
    test -s "$VITASDK/arm-vita-eabi/lib/lib$library.a"
done

cxml="$deps_dir/psp2cxml-tool"
checkout_repository "$(lock_value cxml_tool repository)" "$(lock_value cxml_tool commit)" "$cxml"
(cd "$cxml" && bash -e build_linux_macos.sh)
test -x "$cxml/build/psp2cxml-tool"

# Record the new environment rather than expecting rebuilt libraries to match old binaries.
(
    git -C "$project_dir" rev-parse HEAD
    git -C "$project_dir" submodule status --recursive
    cat "$VITASDK/version_info.txt"
    arm-vita-eabi-gcc --version
    cmake --version
    make --version
    sha256sum "$cxml/build/psp2cxml-tool"
) > "$CI_LOG_DIR/environment.txt"
(
    cd "$VITASDK"
    while IFS= read -r line; do
        sha256sum -- "${line#*  }"
    done < "$project_dir/docs/build-environment.sha256"
) > "$CI_LOG_DIR/rebuilt-sdk.sha256"
echo "$VITASDK/bin" >> "$GITHUB_PATH"
