# Recorded build environment

Recorded on 2026-10-24 from the maintainer's existing Linux installation, with project
source at `51e37381cb450a91ca4cc12d5695fbab9f394f66`. Inspection was read-only apart
from these documentation files. No SDK/library install, configure, compilation, RCO,
packaging or deployment was performed. Existing normal/system CMake caches both point
to `/usr/local/vitasdk`; that identifies the configured environment, not a new build result.
Subsequent maintainer-run CI on `4076f15` successfully rebuilt the pinned dependencies and
both profiles in a fresh runner. The [CI evidence below](#github-actions) is separate from
the initial local snapshot. The maintainer subsequently accepts the requested basic CI-package
Vita smoke (“всё ок”, 2026-10-24); detailed feature/fault and broader compatibility checks remain separate.

## SDK identity

The installed `/usr/local/vitasdk/version_info.txt` reports:

```text
Built at 2026-03-24 15:07:11
newlib            0da3cb1dbdb85932c65b2a0f628364ce0124ea01
pthread-embedded  63e1cd9152082d9ccb1b38d67a4caf975562fbeb
samples           2ec8ca811b782ddc472d0f18d93c929030df4520
vita-headers      ce1a34d9317e33d0dd4facd8b103000f350a78e0
vita-toolchain    71f3789342f610a4eb3ba50fc33292c4900c6666
```

`arm-vita-eabi-gcc` and `g++` report 10.3.0; target GNU ld reports 2.34.
There is no single SDK Git checkout/revision in this installation. The base archive
is now identified as `master-linux-v2.536`; see the verified download below.
Do not substitute a current nightly and call it the recorded SDK.

[build-environment.sha256](build-environment.sha256) records selected installed
SDK inputs relative to `$VITASDK`: compiler/assembler/linker and packaging tools,
CMake toolchain files, every installed header from PAF's `final/include`, the
libraries linked by the application (including system-profile CodecEngine), selected
runtime/dependency headers and package metadata. This is a comparison baseline,
not a complete SDK backup or a guarantee of byte-identical output. It cannot recover
missing archives, and it does not fingerprint every compiler header or host dependency.

## Recovered SDK archive and remaining portlibs

The [official SDK release](https://github.com/vitasdk/autobuilds/releases/tag/master-linux-v2.536)
contains `vitasdk-x86_64-linux-gnu-2026-03-24_14-51-07.tar.bz2`, asset ID
`380523786`, 72791907 bytes. Downloaded SHA256:
`f48b261a5c9b212272af85381dbfc75b0dafe34324f7e8802be3bc8d2393b172`.
The bytes match GitHub's asset digest. Reading the tar without installing it confirms
the complete `version_info.txt` and 57 selected installed inputs byte-for-byte,
including the compiler internals, linker, libc/pthread, Vita stubs and CMake helpers.
The two differing baseline entries are `psp2/paf.h` and `libScePaf_stub_weak.a`,
which the separately pinned PAF component replaces. Additional PAF and portlib inputs
are absent from the base SDK; this is not a complete application dependency bundle.

[build-archives.json](build-archives.json) records the accepted SDK URL/identity,
rejected portlib candidates, and a pinned source-rebuild candidate. To download and
verify the SDK in a separate scratch directory (no install or build):

```bash
curl --fail --location --output vitasdk-x86_64-linux-gnu-2026-03-24_14-51-07.tar.bz2 \
  https://github.com/vitasdk/autobuilds/releases/download/master-linux-v2.536/vitasdk-x86_64-linux-gnu-2026-03-24_14-51-07.tar.bz2
printf '%s  %s\n' f48b261a5c9b212272af85381dbfc75b0dafe34324f7e8802be3bc8d2393b172 \
  vitasdk-x86_64-linux-gnu-2026-03-24_14-51-07.tar.bz2 | sha256sum --check -
```

The [current portlib release](https://github.com/vitasdk/packages/releases/tag/master)
does **not** restore the installed libraries. The six downloaded assets, dated
2026-08-13, have verified GitHub digests but all seven required `.a` files differ,
including their object payloads rather than just archive timestamps. Their compiler
markers say GCC 15.2.0; the installed libraries say GCC 10.3.0. Expat is now 2.8.2,
instead of installed 2.7.4. Other matching version strings/headers are insufficient.
Do not install these assets over the recorded environment as a restoration step.
No matching archives were found in the inspected Downloads, VDPM or system cache
locations; the provider's release list exposes only `master` for these packages.

A [historical recipe snapshot](https://github.com/vitasdk/packages/tree/cf7704284c593c32c30f477dd34c6ff5dbff6cdf)
has the installed package versions and hashes for curl, Expat, Opus, zlib and zstd.
It is a reconstruction candidate, not proof of the installed binaries' origins.
Its OpenSSL recipe uses `git+` without a revision and `SKIP`; pin that source to
[`cd52124ffc8e0f0fc8e2bdde93d4c022a0a7fc53`](https://github.com/d3m3vilurr/vita-openssl/commit/cd52124ffc8e0f0fc8e2bdde93d4c022a0a7fc53)
before any separate rebuild. This was both its current HEAD and latest commit before
the local library timestamps. The candidate build order is zlib, zstd, OpenSSL,
curl, Expat, Opus. Recipes/source hashes are in the JSON. Subsequent CI validates compilation
and packaging from those pins; runtime compatibility still requires Vita acceptance.
Neither a package build nor a dependency replacement was performed by the agent for this comparison.

For the maintainer to repeat the source rebuild, first point `VITASDK` and PATH at a
separate copy of the recovered SDK. The commands below compile/install libraries into
that copy; they have not been run by the agent:

```bash
git clone https://github.com/vitasdk/packages.git vita-portlibs-candidate
cd vita-portlibs-candidate
git checkout --detach cf7704284c593c32c30f477dd34c6ff5dbff6cdf
sed -i 's|vita-openssl.git|vita-openssl.git#commit=cd52124ffc8e0f0fc8e2bdde93d4c022a0a7fc53|' openssl/VITABUILD
for package in zlib zstd openssl curl expat opus; do
  bash build.sh "$package" || exit 1
done
```

The recovered SDK's `vita-makepkg` Git source handler supports `#commit=`; this
closes the recipe's moving-HEAD input while retaining its existing build options.
Retain the generated package archives, their checksums, build logs and the changed
OpenSSL recipe with the new environment record. A failed package must be resolved
before proceeding to PAF/application builds; fresh build/Vita acceptance remains required.

## Pinned source dependencies

- [vitasdk-paf-component](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/tree/9a68b07aa2d7247a756765bf154f292cda20aed3):
  `9a68b07aa2d7247a756765bf154f292cda20aed3`.
- Its installer checks out [ScePaf-RE](https://github.com/GrapheneCt/ScePaf-RE/tree/b76d6ae00516b107d37e7e8a1a16211e3b729a07)
  at `b76d6ae00516b107d37e7e8a1a16211e3b729a07`, applies `vitasdk_2.patch`,
  then overlays the component's map/math/`psp2/paf.h` adapters. Patch SHA256:
  `aa81f526194e50c0f44af22d1c07b24cc8027a51c618344a119e93c5c76209dd`.
  Applying that patch to a temporary copy of the pinned source reproduces the local
  patched headers. All 205 `final/include` files and all `final/lib` archives match
  the installed SDK copies. The two modified tracked installer/sample scripts differ
  only by executable permission; there are no extra tracked content patches.
- [psp2cxml-tool](https://github.com/Princess-of-Sleeping/psp2cxml-tool/tree/d971a9a9892c122b4edef15d335326b2a551fec4):
  `d971a9a9892c122b4edef15d335326b2a551fec4`. The local build-script change is
  executable permission only. Its CMake file pins host-side zlib 1.3.1 by SHA256
  `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`.
  The executable selected by the project's current fallback order is
  `/home/nowaru/Desktop/psp2cxml-tool/build/psp2cxml-tool`. It is byte-identical
  to `/home/nowaru/bin/psp2cxml-tool` and the source-root copy (638704 bytes), SHA256
  `8a602caac3c9c7cc86098f15a496c372d103995157169f2df16a5e85883b55a0`.
  Pin the source for rebuilds; host compiler differences can change the executable hash.
- Local [VDPM](https://github.com/vitasdk/vdpm/tree/e423be457622a9ba517858f4831566dcd263729a):
  `e423be457622a9ba517858f4831566dcd263729a` (clean checkout). Its installer fetches
  libraries from the mutable `vitasdk/packages` release named `master`. Pinning VDPM
  alone therefore does not pin package contents.
- Project submodules: Moonlight common-c `2d984f4b0dfab5ea2cff0f262abe1a056946a7c1`;
  ENet `dea6fb5414b180908b58c0293c831105b5d124dd` (both the root ENet checkout and
  common-c's nested ENet). Use the project's recorded gitlinks, not upstream HEAD.
  inih and h264bitstream are vendored project files, versioned with this repository.

Installed target package metadata reports libcurl **8.17.0**, OpenSSL **1.0.2i-dev**,
Expat **2.7.4**, Opus **1.6.1**, zlib **1.3.2**, and zstd **1.5.7**. libcurl's `.pc`
requires OpenSSL, zlib and zstd; the project also explicitly links these libraries.
These are local metadata values, not independently recovered package source revisions.
The archive fingerprints preserve a stronger identity than version strings alone.
The host-side zlib used to build CXML is separate from the Vita target zlib.

## Host tools and environment

Observed host: Fedora Linux 43, x86_64. CMake **3.31.11**, GNU Make **4.4.1**,
host GCC **15.2.1 20260123 (Red Hat 15.2.1-7)**, Git **2.53.0**, pkg-config
**2.3.0**, Python **3.14.3**, pycparser **2.22**. The CXML cache points to host
`/usr/lib64/ccache/cc` and `c++`; it is not a Vita executable. The installed SDK's
CMake helpers require at least CMake 3.16, despite the application's older minimum.
CMake 4 has not been validated with these older dependency projects.

Host prerequisites: Bash, CMake, Make, host C/C++ compiler, Git, patch, curl/wget,
tar with bzip2/xz support, zip and `makedepend` (Ubuntu package `xutils-dev`, needed
by the pinned OpenSSL recipe's `make depend`). Python plus pycparser are needed only for the
non-build source checks. `psp2shell_cli` is optional for external deployment/log
tooling; it is not used by the checked-in build scripts.

For an installed SDK and a CXML executable built from the pinned source:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
export PSP2CXML_TOOL="$HOME/Desktop/psp2cxml-tool/build/psp2cxml-tool"
```

Set `PSP2CXML_TOOL` explicitly to avoid another executable winning fallback lookup.
The source directory can be elsewhere; change that path accordingly. The current
installation had `PSP2CXML_TOOL` unset, so the sibling `../psp2cxml-tool/build/`
candidate won before PATH lookup. No general dump of environment variables is needed.

## Setup procedure (maintainer reference; automated path verified in CI)

1. Use the verified SDK archive above in a separate environment; its top-level directory
   is `vitasdk/`. Original target portlib archives are still missing. Exact restoration
   requires those archives; alternatively, the maintainer can build and validate a new
   baseline using the pinned recipes, as the successful CI run does. Keep the current working
   installation until the new packages pass Vita acceptance. Running `vitasdk-update`, the VDPM bootstrap or `install-all.sh`
   against a moving release is not a reproduction of this baseline.
2. Install the host prerequisites and export `VITASDK`/PATH as above. Use a new writable
   dependency directory for the following commands. They build/install dependencies
   and are provided for the maintainer, not for agent execution:

   ```bash
   git clone https://github.com/Princess-of-Sleeping/vitasdk-paf-component.git
   cd vitasdk-paf-component
   git checkout --detach 9a68b07aa2d7247a756765bf154f292cda20aed3
   bash install.sh
   cd ..
   git clone https://github.com/Princess-of-Sleeping/psp2cxml-tool.git
   cd psp2cxml-tool
   git checkout --detach d971a9a9892c122b4edef15d335326b2a551fec4
   bash build_linux_macos.sh
   export PSP2CXML_TOOL="$PWD/build/psp2cxml-tool"
   cd ..
   ```

   PAF's installer builds its host helper and Vita stubs, patches the pinned RE
   headers, and replaces the SDK's original PAF headers/stubs. Start from a fresh
   clone: an existing `build/` directory causes upstream install steps to be skipped.
   The CXML build downloads its pinned host zlib source and needs network access.
3. Clone the application recursively at the intended project revision:

   ```bash
   git clone --recurse-submodules https://github.com/NowaLone/vita-moonlight-paf.git
   cd vita-moonlight-paf
   git submodule update --init --recursive
   git submodule status --recursive
   ```

   For an older release, check out its exact project commit before updating submodules.
   A fresh checkout provides an empty build tree without deleting existing artifacts.
4. Compare the installed SDK with the recorded fingerprints (read-only, no build):

   ```bash
   snapshot_manifest="$PWD/docs/build-environment.sha256"
   (cd "$VITASDK" && sha256sum --check "$snapshot_manifest")
   sha256sum "$PSP2CXML_TOOL"
   PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py'
   ```

   A fingerprint difference needs explanation; it is not proof of incompatibility.
   Source-rebuilt PAF archives and host tools can differ in binary metadata. Record
   their new hashes and validate the new environment instead of silently replacing
   the baseline. Source checks do not compile the application or validate Vita ABI.
5. The maintainer runs `bash build.sh` from the fresh checkout. This compiles CXML once,
   then normal/system profiles in separate trees; the script resets diagnostic test
   modes and heap tracing OFF by default. Both packages use `VLMP00001` and replace
   each other when installed. Keep matching debug ELF files with each VPK/SELF.
   For outputs, single-profile options and system memory budgets see [README](../README.md#build).
6. Record project revision, environment/fingerprint differences, full build result and
   warnings, then install each full VPK and record the Vita smoke result/profile.
   Only after this succeeds is a clean-environment reproduction established.

## GitHub Actions

[Clean Vita build](../.github/workflows/build.yml) implements the candidate setup through
[scripts/ci_setup.sh](../scripts/ci_setup.sh). After the workflow reaches the default branch,
the maintainer starts it under **Actions → Clean Vita build → Run workflow**. There are no
push/PR/scheduled triggers, dependency caches, deployment or Release publication. The job has
read-only repository permissions; checkout and artifact actions use full commit pins.

The `ubuntu-22.04` runner installs host prerequisites and a Python virtual environment with
CMake 3.31.10/pycparser 2.22, then runs the source checks. CI uses the published PyPI CMake
3.31.10 wheel; the inspected Fedora host has 3.31.11. Setup verifies the SDK SHA256 before
extraction, rebuilds the six portlibs from the recorded recipe commit, adds the explicit OpenSSL
source pin, verifies the PAF patch and RE base, and builds the pinned host CXML tool. The
application's existing `build.sh` then builds normal/system profiles. A pre-existing dependency
directory is rejected to prevent cache reuse or replacement of a working SDK.

Two artifacts are retained for 14 days (subject to repository retention policy):

- `vita-moonlight-paf-<commit>`: both VPKs, SELF files and their matching unstripped debug ELF files,
  uploaded only after a successful build and checks that all six outputs exist and are nonempty.
- `vita-build-evidence-<commit>-<attempt>`: setup/source/build logs, source pins, the modified
  OpenSSL recipe, rebuilt portlib package archives, package/application checksums, environment
  versions and the rebuilt SDK's selected file hashes. Upload is attempted even on failure.

CI records new library fingerprints rather than requiring source-built libraries to match the
old installation byte-for-byte. The runner image and distribution host packages can change;
this workflow is a clean setup/build check, not a bit-for-bit reproducible-build claim. No
secrets or Vita credentials are needed. Dependency install/build commands execute only when
the maintainer starts the workflow. Static validation and isolated helper tests do not establish
a successful SDK/library/application build. The agent has not run a build locally or dispatched CI.

The maintainer supplied the log of [run 40051978775](https://github.com/NowaLone/vita-moonlight-paf/actions/runs/40051978775)
on `899cc3c`, Ubuntu image `20261012.322.1` (2026-10-24). Checkout/submodules and host-tool
installation succeeded, including CMake 3.31.10/pycparser 2.22. Of 208 source checks, one
failed because the RGB mask test required compressed PNG byte equality. SDK/dependency and
application builds did not start; evidence-artifact upload succeeded. The test now compares
IHDR and decompressed scanlines with CRC/geometry checks and covers an alternate compression
level. A successful CI build on the corrected source and subsequent Vita smoke remain pending.

The next supplied log, [run 40052361277](https://github.com/NowaLone/vita-moonlight-paf/actions/runs/40052361277)
on `0d4624d`, passes all 208 source checks and verifies the SDK archive. zlib 1.3.2-2 and
zstd 1.5.7-1 finish building; pinned OpenSSL reaches `make depend` and fails because
`makedepend` is missing. Evidence upload succeeds. Host setup now installs Ubuntu's
`xutils-dev` and checks that `makedepend` is available before dependency setup. Completion
of OpenSSL/remaining dependencies and application builds is still unverified.

**Clean build passed, 2026-10-24:** the maintainer supplied the complete log of
[run 40052743854](https://github.com/NowaLone/vita-moonlight-paf/actions/runs/40052743854) on
`4076f15442064d92f368a64f8ae4bf75ac75fd34`, Ubuntu image `20261012.322.1`.
All 208 source checks pass, the SDK hash is verified, and zlib 1.3.2-2, zstd 1.5.7-1,
OpenSSL 1.0.2-1, curl 8.17.0-2, Expat 2.7.4-1 and Opus 1.6.1-1 finish building.
The pinned PAF component installs, both CXML-tool targets build, and CXML-to-RCO succeeds.
`build.sh` completes NORMAL (`gd`) and SYSTEM (`gdc`, 65536 KiB, attribute `0x08`) builds.
All six profile-suffixed VPK/SELF/ELF files pass the workflow's nonempty-file checks.

The [application artifact](https://github.com/NowaLone/vita-moonlight-paf/actions/runs/40052743854/artifacts/12544391557)
uploads six files, and the [build-evidence artifact](https://github.com/NowaLone/vita-moonlight-paf/actions/runs/40052743854/artifacts/12543854745)
uploads 16 files including logs, portlib archives and generated checksums/fingerprints.
Run identity, artifact IDs and the upload-reported ZIP digests are recorded in
[build-archives.json](build-archives.json). The agent inspected the supplied log, did not
download or independently inspect these artifacts, and did not build or dispatch CI.
This closes the clean CI setup/dependency/application build gate.

**Basic Vita smoke accepted, 2026-10-24:** after receiving the CI artifact link and the request
to check those VPKs for startup, Settings, streaming and reconnect, the maintainer replied
“всё ок”. Record this as user-reported acceptance of that basic CI-package check. No new log,
separate per-profile result, firmware or host version was supplied. This does not certify the
entire UI/statistics, fault/security, motion or endurance matrix, and no agent device run occurred.

## Known warnings and remaining evidence

Earlier maintainer records mention SDK signedness/`wchar_t` warnings and future-resource
timestamp/clock-skew warnings. These are historical observations; no new warning log was
produced during the initial local inspection. The successful CI log includes OpenSSL
`makedepend` missing-host-include warnings, compiler signedness and host-tool format warnings,
4-byte/2-byte `wchar_t` linker warnings, CMake deprecation/developer warnings and jobserver
warnings. These did not stop the build; the result does not establish ABI or runtime correctness.
The current CMake fallback also warns when
`$VITASDK/../vitasdk-paf-component/paf_samples/vitasdk` is absent (as in this installation).
PAF stubs are installed under `$VITASDK/arm-vita-eabi/lib`; do not mistake the optional
samples-directory warning for permission to use stock VitaSDK PAF headers.

The new source-based baseline has a successful fresh CI build and user-confirmed basic Vita
smoke. Release readiness, detailed feature/fault checks and broader host/device compatibility
remain open. The original
local portlib archives remain unrecovered; CI success does not establish byte-identical
reproduction of that old installation or certify other toolchain/host combinations.
