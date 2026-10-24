# Recorded build environment

Recorded on 2026-10-24 from the maintainer's existing Linux installation, with project
source at `51e37381cb450a91ca4cc12d5695fbab9f394f66`. Inspection was read-only apart
from these documentation files. No SDK/library install, configure, compilation, RCO,
packaging or deployment was performed. Existing normal/system CMake caches both point
to `/usr/local/vitasdk`; that identifies the configured environment, not a new build result.

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
There is no single SDK Git checkout/revision in this installation. The original
autobuild release tag, archive URL and archive SHA256 have not been recovered.
Do not substitute a current nightly and call it the recorded SDK.

[build-environment.sha256](build-environment.sha256) records selected installed
SDK inputs relative to `$VITASDK`: compiler/assembler/linker and packaging tools,
CMake toolchain files, every installed header from PAF's `final/include`, the
libraries linked by the application (including system-profile CodecEngine), selected
runtime/dependency headers and package metadata. This is a comparison baseline,
not a complete SDK backup or a guarantee of byte-identical output. It cannot recover
missing archives, and it does not fingerprint every compiler header or host dependency.

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
tar with bzip2/xz support and zip. Python plus pycparser are needed only for the
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

## Setup procedure (maintainer commands; fresh environment not yet validated)

1. Provision the matching SDK and the target libraries above. The original SDK and
   portlib archive identities remain a prerequisite for exact restoration. Keep the
   current installation until matching archives or a separately verified replacement
   are available; running `vitasdk-update`, the VDPM bootstrap or `install-all.sh`
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

## Known warnings and remaining evidence

Earlier maintainer records mention SDK signedness/`wchar_t` warnings and future-resource
timestamp/clock-skew warnings. These are historical observations; no new warning log was
produced here. The current CMake fallback also warns when
`$VITASDK/../vitasdk-paf-component/paf_samples/vitasdk` is absent (as in this installation).
PAF stubs are installed under `$VITASDK/arm-vita-eabi/lib`; do not mistake the optional
samples-directory warning for permission to use stock VitaSDK PAF headers.

Still open: recover immutable SDK/portlib archive references or establish a new complete
baseline, run the documented setup in a fresh environment, and record maintainer build/Vita
results. This document pins the inspected installation; it does not claim a clean build
or certify all combinations of toolchain/host versions.
