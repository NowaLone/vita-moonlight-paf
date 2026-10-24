# Licensing and attribution review

Reviewed 2026-10-24 against the source tree and the pinned clean-build baseline in
[BUILD_ENVIRONMENT.md](BUILD_ENVIRONMENT.md). This records identified terms and
release work; it does **not** clear the current binaries for redistribution.
Upstream notices remain authoritative. Copies below do not grant rights to other
files, Sony artwork, or the project's own contributions.

## Project license and release blockers

There is no top-level project license yet. The executable incorporates GPLv3
Moonlight code; an MIT-only or proprietary grant for the combined application
would not satisfy those inherited terms. A GPLv3 project license is the proposed
direction, subject to confirming rights in the project's contributions and the
borrowed material below. Do not invent a linking exception on behalf of upstream
copyright holders.

The licensing review is recorded; resolution remains open:

- **Old OpenSSL and GPL.** The build statically links `libssl`/`libcrypto` from
  `vita-openssl` commit `cd52124ffc8e0f0fc8e2bdde93d4c022a0a7fc53` (1.0.2i-dev).
  This uses the original OpenSSL **and** SSLeay terms, not Apache-2.0. The FSF
  identifies that old combination as GPL-incompatible. No applicable OpenSSL
  linking exception was found in the reviewed Moonlight license files/headers.
  Before releasing, establish applicable permission from the relevant holders or
  separately implement and validate a compatible crypto dependency. Adding an
  exception only for new project code cannot authorize all inherited code.
  Keep the required OpenSSL-backed certificate verification intact while this
  remains unresolved; this review makes no TLS/backend change.
  Sources: [pinned license](https://github.com/d3m3vilurr/vita-openssl/blob/cd52124ffc8e0f0fc8e2bdde93d4c022a0a7fc53/LICENSE),
  [OpenSSL's version distinction](https://openssl-library.org/source/license/index.html),
  [FSF compatibility assessment](https://www.gnu.org/licenses/license-list.html#OpenSSL).
- **Redistributed artwork.** `pc.png` and the two availability PNGs are extracted
  Sony assets. No redistribution grant was found. Obtain permission or replace
  them with independently authored, licensed artwork. Confirm the origins/rights
  of the remaining assets listed below, including the supplied Add PC button.
  Extraction, conversion, modification and attribution alone do not supply a grant.
- **PAF declarations and borrowed frontend material.** The PAF adapter's MIT
  license does not establish the terms of its ScePaf-RE input. The pinned
  [ScePaf-RE tree](https://github.com/GrapheneCt/ScePaf-RE/tree/b76d6ae00516b107d37e7e8a1a16211e3b729a07)
  has no separate license file; its README describes recovered DWARF declarations
  and reverse engineering. Confirm permissions for reused declarations/templates,
  including `include/platform/paf_common_gui_dialog.h`. NetStream/BHBB are cited
  as frontend references in this repository; distinguish independently written
  API usage/layout from any copied expressive code/resources and establish terms
  for the latter. The inspected
  [NetStream tree](https://github.com/GrapheneCt/NetStream/tree/99db706d860f7126b0b8b6ac1f5a5bbf960055f8)
  also has no license file. Availability on GitHub is not a substitute for terms.
- **mDNS grant.** Preserve AorsiniYT's attribution and original README permission
  below; obtain a precise license clarification before labeling the code MIT.
- **Project-owned contributions.** Confirm the contributor rights and intended
  GPLv3 grant, then add the top-level license and scope notice. Do not apply that
  grant to independently licensed dependencies or unresolved assets.
- **Distribution materials.** Complete the notices/source/relinking steps below
  and check them in the actual release artifacts. Current CMake VPK packaging and
  CI artifact uploads do not include this license bundle or corresponding source.

## Incorporated source and attribution

- **Moonlight / Vita Moonlight / Moonlight Embedded.** The legacy GameStream
  transport, device-store lineage and Vita streaming implementation derive from
  [xyzz/vita-moonlight](https://github.com/xyzz/vita-moonlight/tree/984603bd6f93f752593048fe494b5ffca14514e1)
  and its contributors. `src/backend/legacy/legacy_gamestream.c` credits
  Copyright (C) 2015-2017 Iwan Timmer; `src/moonlight/sps.c` and `sps.h` credit
  Copyright (C) 2015 Iwan Timmer. Their headers grant GPL-3.0-or-later.
  `vita_audio_renderer.c` explicitly records its xyzz/Vita origin. Preserve these
  notices and the broader Vita Moonlight contributor attribution for adapted
  rendering/input/storage code. The upstream project carries GPLv3; a verbatim
  GPLv3 text is available at [LICENSES/GPL-3.0.txt](../LICENSES/GPL-3.0.txt).
  Local adaptations are recorded in this repository's 2026 Git history; distribute
  the relevant modification notices and source with a release.
- **moonlight-common-c**, Moonlight contributors, submodule
  `2d984f4b0dfab5ea2cff0f262abe1a056946a7c1`: GPLv3 text in
  [its license](../third_party/moonlight-common-c/LICENSE.txt). The selected source
  files are compiled directly into the executable by the root `CMakeLists.txt`.
- **Reed-Solomon FEC**, included through common-c's `reedsolomon/rs.c`: separate
  BSD-2-Clause notice, Copyright 1997-98 Luigi Rizzo, 2001 Alain Knaff and
  2017 Iwan Timmer; portions credited to Phil Karn, Robert Morelos-Zaragoza and
  Hari Thirumoorthy. The [complete source-header notice](../LICENSES/reedsolomon.txt)
  is retained separately for binary-distribution materials.
- **ENet**, Copyright (c) 2002-2020 Lee Salzman: MIT, submodule
  `dea6fb5414b180908b58c0293c831105b5d124dd`, [license](../third_party/enet/LICENSE).
  The root ENet checkout is compiled; common-c's nested ENet is not separately
  linked by the root build. Retain its license when distributing that source too.
- **inih**, Copyright (C) 2009-2025 Ben Hoyt in the source headers: BSD-3-Clause.
  The missing [LICENSE.txt](../third_party/inih/LICENSE.txt) is restored verbatim
  from [8e06f6b77b5d4471bdc6d85ada81b67d37354a5c](https://github.com/benhoyt/inih/blob/8e06f6b77b5d4471bdc6d85ada81b67d37354a5c/LICENSE.txt),
  whose license text uses the original 2009 copyright. Local `ini.c` matches that
  source snapshot via the Vita Moonlight checkout; `ini.h` is adapted.
- **h264bitstream**, Copyright (C) 2005-2007 Auroras Entertainment, LLC,
  2008-2011 Avail-TVN and 2012 Alex Izvorski in the included headers; written by
  Alex Izvorski and Alex Giladi: LGPL-2.1-or-later. The missing
  [LICENSE](../third_party/h264bitstream/LICENSE) is restored verbatim from
  [Vita Moonlight's vendored copy](https://github.com/xyzz/vita-moonlight/blob/984603bd6f93f752593048fe494b5ffca14514e1/third_party/h264bitstream/LICENSE).
  Five of the six vendored source/header files match that snapshot byte-for-byte;
  `h264_nal.c` is locally modified. Preserve the per-file notices and LGPL terms.
- **mDNS sniffer**, `src/backend/legacy/mdns/udp_sniffer_vita.{c,h}`:
  AorsiniYT, Copyright 2025, adapted from
  [mdnsniff 6496c321c51860c5b64c308cff076b6087b40be0](https://github.com/AorsiniYT/mdnsniff/tree/6496c321c51860c5b64c308cff076b6087b40be0).
  Its [README permission](https://github.com/AorsiniYT/mdnsniff/blob/6496c321c51860c5b64c308cff076b6087b40be0/README.md#licencia)
  reads: “MIT o similar. Puedes usar, modificar y compartir este código citando al autor.”
  This is evidence of permission with attribution, but not an unambiguous standard
  MIT license text. Keep that distinction; no replacement grant is added here.

## Statically linked SDK libraries

These are the pinned clean-CI recipe versions, not the rejected newer portlib
downloads. Source pins, recipe patches and archive hashes are in
[build-archives.json](build-archives.json). Copies preserve upstream wording.

- **curl 8.17.0**, Daniel Stenberg and contributors: curl permission notice.
  [Local text](../LICENSES/curl.txt),
  [upstream COPYING](https://github.com/curl/curl/blob/curl-8_17_0/COPYING).
- **Expat 2.7.4**, Thai Open Source Software Center Ltd, Clark Cooper and Expat
  maintainers: MIT. [Local text](../LICENSES/expat.txt),
  [upstream COPYING](https://github.com/libexpat/libexpat/blob/R_2_7_4/expat/COPYING).
- **OpenSSL 1.0.2i-dev**, OpenSSL Project, Eric Young and Tim Hudson: original
  OpenSSL + SSLeay terms. [Local full dual-license text](../LICENSES/openssl.txt),
  [pinned upstream LICENSE](https://github.com/d3m3vilurr/vita-openssl/blob/cd52124ffc8e0f0fc8e2bdde93d4c022a0a7fc53/LICENSE).
  Preserve the required OpenSSL/Eric Young acknowledgments and applicable
  advertising notices. Compatibility remains unresolved above.
- **zlib 1.3.2**, Jean-loup Gailly and Mark Adler: zlib license.
  [Local text](../LICENSES/zlib.txt),
  [upstream LICENSE](https://github.com/madler/zlib/blob/v1.3.2/LICENSE).
- **Zstandard 1.5.7**, Meta Platforms, Inc. and affiliates: BSD-3-Clause option
  of the upstream dual BSD/GPLv2 licensing.
  [Local BSD text](../LICENSES/zstd.txt),
  [upstream LICENSE](https://github.com/facebook/zstd/blob/v1.5.7/LICENSE).
- **Opus 1.6.1**, Xiph.Org and the other holders listed in COPYING: BSD-3-Clause,
  with the patent-license references retained in the [local text](../LICENSES/opus.txt)
  from [upstream COPYING](https://github.com/xiph/opus/blob/v1.6.1/COPYING).
- **newlib/libm**, SDK revision `0da3cb1dbdb85932c65b2a0f628364ce0124ea01`:
  multiple file-specific permissive notices, not one blanket MIT grant. Preserve
  the [full COPYING.NEWLIB](../LICENSES/newlib.txt) from
  [that revision](https://github.com/vitasdk/newlib/blob/0da3cb1dbdb85932c65b2a0f628364ce0124ea01/COPYING.NEWLIB)
  and check any additional notices in the actual linked objects before release.
- **pthread-embedded**, SDK revision `63e1cd9152082d9ccb1b38d67a4caf975562fbeb`:
  inherited pthreads-win32 LGPL-2.1 terms and Davee's MIT Vita port.
  Preserve [COPYING](../LICENSES/pthread-embedded.txt),
  [COPYING.LIB](../LICENSES/pthread-embedded-LGPL.txt) and
  [COPYING.vita](../LICENSES/pthread-embedded-vita.txt), copied from the
  [pinned source](https://github.com/vitasdk/pthread-embedded/tree/63e1cd9152082d9ccb1b38d67a4caf975562fbeb).
- **GCC 10.3.0 libgcc/compiler runtime**: GPLv3 with the GCC Runtime Library
  Exception where the source applies it. Preserve the [GPLv3 text](../LICENSES/GPL-3.0.txt)
  and [exception](../LICENSES/gcc-runtime-exception.txt), copied from
  [GCC 10.3.0](https://github.com/gcc-mirror/gcc/blob/releases/gcc-10.3.0/COPYING.RUNTIME).
  This exception is specific to the compiler runtime; it supplies no OpenSSL exception.
- **vita-headers / generated import stubs**, SDK revision
  `ce1a34d9317e33d0dd4facd8b103000f350a78e0`: MIT, Copyright 2017 vitasdk.
  [Local text](../LICENSES/vita-headers.txt),
  [upstream license](https://github.com/vitasdk/vita-headers/blob/ce1a34d9317e33d0dd4facd8b103000f350a78e0/LICENSE.md).
- **vitasdk-paf-component**, `9a68b07aa2d7247a756765bf154f292cda20aed3`:
  MIT, Copyright (c) 2025 Princess-of-Sleeping.
  [Local text](../LICENSES/vitasdk-paf-component.txt),
  [upstream license](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/blob/9a68b07aa2d7247a756765bf154f292cda20aed3/LICENSE).
  This covers the adapter's own grant; see the separate ScePaf-RE question above.

The current VPK manifest does not package Sony PAF/AppSettings modules; the app
loads them from the Vita. Runtime use of installed system modules is distinct
from shipping their code or extracted artwork. Host-only tools such as
psp2cxml-tool are also distinct from linked target libraries; include their terms
if redistributing a toolchain/tool bundle. The source/build bundle still needs
the exact tool identities and scripts required to reproduce the executable.

## Packaged artwork inventory

All current PNGs are covered by these groups; appearance validation is not rights
clearance. Resource provenance evidence is in [UI.md](UI.md).

- `cxml/texture/pc.png`: extracted Sony Music `musicbrowser_plugin.rco` PC icon.
- `cxml/texture/host_available.png` and `host_unconfirmed.png`: Sony Wi-Fi settings
  `wifi_settings_plugin.rco`, texture IDs `fbd879ae` and `9718c305`.
- `cxml/texture/tx_man.png`: maintainer-supplied finished Add PC artwork,
  SHA256 `83fb42a9a4682f3d9c6bcaf3780410f90bbedefd22077caaa996c4d5c8896fb0`.
  Its author/source rights remain to be confirmed. Earlier Photos-derived button
  PNGs were removed; removal does not establish this replacement's license.
- `cxml/texture/bg.png`: present in initial commit `04937a7`; author/source and
  redistribution terms not established by that commit.
- `cxml/texture/controller_light_{ds4,left,right}.png`: project-authored masks,
  generated by `tools/generate_controller_light_textures.py` and revised in Git.
  They need the project's own license grant; no extracted Sony texture is claimed.
- `sce_sys/icon0.png`, `pic0.png`, `livearea/contents/{bg0,startup,frame1,frame2,frame3,frame4,frame5,frame6,frame7}.png`
  and `manual/{001,002,003}.png`: supplied/updated LiveArea and manual artwork.
  Commit `ffea482` describes an original Moonlight-logo basis; later artwork has
  multiple revisions. Confirm logo, image/screenshot and derivative permissions
  for the final files rather than assuming a source-code license covers images.

`_common_texture_*` references resolve from the Vita's installed common PAF
resource. They are not standalone texture files packaged by this repository.
Do not bundle extracted common RCOs or Sony modules as a way to resolve a rights gap.

## Release distribution checks

After resolving the rights/compatibility questions:

1. Add the agreed project license/scope, preserve upstream per-file notices, and
   prepare final third-party acknowledgments using the texts above. Include them
   with both VPK profiles and standalone SELF distributions. Verify the resulting
   package contents; they are not packaged by this documentation-only change.
2. Offer the exact corresponding source alongside the binaries under GPLv3
   section 6, including populated submodules (GitHub's automatic source archive
   alone omits their contents), vendored sources, dependency source/patches,
   resource sources, build scripts and clear reproducible setup instructions.
   Identify the release commit and dependency pins next to the download.
3. Preserve the LGPL library sources/modifications and provide the materials and
   instructions needed to relink a modified library with the application. Check
   the chosen LGPL distribution route for h264bitstream and pthread-embedded;
   merely adding their license texts or a repository link is not the whole step.
4. Check file-specific notices against the actual final link inputs, not just the
   library names. Finish any required interactive legal-notice treatment under
   GPLv3 section 5(d), assessing its upstream-interface exception before choosing
   a UI change. No unrequested About/settings page is added by this review.
5. Have the maintainer build and inspect the final release packages and repeat
   the release-candidate Vita smoke check. The earlier CI/Vita acceptance predates
   any future licensing/package changes and does not validate those artifacts.
