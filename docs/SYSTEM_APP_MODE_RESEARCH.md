# Launching Moonlight alongside an existing application

Research date: 2026-10-19. Moonlight baseline: `dcf273f4c3a22b06429a193544fb217567b0b0b5`.

## Result

**System application mode is the concrete candidate.** BetterHomebrewBrowser supplies
a small SELF memory budget and system-style packaging. VitaSDK already exposes the
relevant packaging controls, so changing compiler/toolchain is not a prerequisite.
This is a source-supported experiment, not verified Moonlight coexistence support.

The intended acceptance case is: suspend a Vita game, open Moonlight without closing
that game, use Moonlight, then return to the same game session. This does not promise
that the suspended game executes alongside streaming, or that every application can
remain resident under every memory configuration.

Do not implement this by switching to SAFE, replacing AppSettings, patching SceShell,
or moving streaming into the PAF plugin. The existing UNSAFE/native architecture is
compatible with investigating system application budgets.

## BetterHomebrewBrowser reference

Checked the clean local checkout `/home/nowaru/Desktop/BetterHomebrewBrowser`, revision
`9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5`. These links identify the inspected files;
web retrieval of some pinned/raw files failed, so their contents were verified locally.

- [bootparam.yml](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/bootparam.yml):
  `APP_MEMSIZE: 32768` (32 MiB) and boot `ATTRIBUTE: 8`.
- [param.sfx](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/param.sfx):
  `CATEGORY=gdc`, SFO `ATTRIBUTE=32832` (`0x8040`), `ATTRIBUTE_MINOR=16`, `ATTRIBUTE2=0`.
- [CMakeLists.txt](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/CMakeLists.txt#L190-L191):
  signing explicitly consumes `bootparam.yml`, with authority ID `2200000000000001`.
- [src/main.cpp](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/src/main.cpp):
  sets PAF `Framework::Mode_Application` **after** `Framework::SampleInit()`, with an
  8 MiB surface pool and a 4 MiB text-surface pool.
- [src/paf_runtime.cpp](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/src/paf_runtime.cpp):
  loads PAF with a 10 MiB global heap. Its Sony runtime differs from Moonlight's newlib.

These are separate controls: SFO category/attributes, SELF boot attributes and budgets,
SELF authority, and PAF rendering mode. Copying only `CATEGORY=gdc` does not establish
the same launch behavior. The reference also has a separate background-download
component; reproducing it is unnecessary for the requested foreground launch experiment.

## VitaSDK support and an existing packaging trap

The installed `/usr/local/vitasdk/share/vita.cmake` maps `MEMSIZE` to `-m` and `ATTRIBUTE`
to `-at`; physical reservation can be passed as `-pm` through `VITA_MAKE_FSELF_FLAGS`.
The [upstream helper](https://github.com/vitasdk/vita-toolchain/blob/master/cmake_toolchain/vita.cmake)
and [signer](https://github.com/vitasdk/vita-toolchain/blob/master/src/vita-make-fself/vita-make-fself.c)
confirm KiB units, a documented system budget range of 4–74 MiB, and that physical
reservation is deducted from the total budget.

Crucially, the signer writes the requested boot attributes **only when `mem_budget`
is nonzero**. Current Moonlight requests `UNSAFE ATTRIBUTE 0x0E` without `MEMSIZE`;
the apparent attribute therefore does not describe the generated control-info word.
Adding a budget would activate it too, so retaining `0x0E` is itself a behavior change.

Read-only parsing of the existing ignored `build/vita_moonlight_paf.self` found:

```text
authority ID:                 0x2200000000000001
SELF control info 6 is_used:  1
SELF control info 6 attr:     0x00000000
physical budget (KiB):       0
total budget (KiB):          0
generated SFO CATEGORY:      gd
generated SFO ATTRIBUTE:     33472 (0x82c0)
generated ATTRIBUTE_MINOR:   16
```

The existing generated command contains `-at 0x0E` and no `-m`. These artifacts were
inspected, not rebuilt, and are not proof of the package currently installed on Vita.
Source `CMakeLists.txt` agrees on these requested settings. Authority already matches
BHBB; another authority experiment is unnecessary.

The [Volume Control Pack author's instructions](https://github.com/GrapheneCt/Volume-Control-Pack)
identify boot attribute bit `0x08` with access to all cores in that system-mode example.
Do not describe this bit alone as a universal multitasking switch, and do not confuse
boot attributes with SFO `ATTRIBUTE`.

## Moonlight-specific obstacles

### 1. Newlib currently requests 128 MiB before normal operation

`src/main.cpp` manually calls `_init_vita_heap()` after loading PAF. No project source
defines `_newlib_heap_size_user`. The existing ELF contains that symbol as undefined
weak, and disassembling `_init_vita_heap()` confirms a default `0x08000000` allocation.
This matches [VitaSDK newlib's sbrk.c](https://github.com/vitasdk/newlib/blob/vita/newlib/libc/sys/vita/sbrk.c):
the default is 128 MiB and allocation failure leaves the newlib heap unavailable.

Consequently, a 32 MiB boot budget cannot work with the current initialization as-is.
A prototype must explicitly cap the newlib heap through its C-linkage override and
measure actual usage/headroom. Choosing a production heap size requires measurements.

C++ `new`/`delete` use PAF, but C `malloc`/`calloc` and dependent libraries still use
newlib. Reducing the PAF heap alone does not fix this second allocator.

### 2. PAF and graphics allocations need their own budget

Moonlight loads PAF with a 16 MiB global heap, versus BHBB's 10 MiB. Its framework uses
`Mode_Normal`, assigned before `SampleInit()`, and `graphics_option=7`; it does not
explicitly size the framework pools. Investigate `Mode_Application` after `SampleInit()`
and bounded pools in a controlled prototype. A PAF framework mode is not an OS launch
mode, and blindly copying pool sizes would not establish streaming capacity.

Network initialization additionally reserves 1 MiB **from the PAF heap**; do not count
that as an independent 1 MiB on top of the entire PAF heap. Module code/data, native
dialogs, AppSettings, TLS, queues and thread stacks also need headroom.

### 3. Streaming requires physical memory and CDRAM

`src/moonlight/vita_video_renderer.c` queries `frameMemSize` from the AVC decoder,
rounds it up to 1 MiB, and allocates `USER_MAIN_PHYCONT_NC_RW`. It also allocates three
RGBA framebuffers in `USER_CDRAM_RW`, each rounded to 256 KiB. At an output size of
960×544 these alone occupy 6 MiB in CDRAM; other aspect ratios/crop modes can differ.
That figure excludes PAF graphics allocations and decoder working memory.

The [AppMgr header](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/appmgr.h)
distinguishes application mode A (no physical-contiguous access), B (physical access),
and C (physical plus extra memory). BHBB's small frontend budget does not prove that
Moonlight's AVC path can allocate its required memory beside a suspended game.

A streaming prototype must inspect `sceAppMgrGetBudgetInfo()` and its total/free
USER_RW, PHYCONT, CDRAM and extra-memory fields. Check the return value first: the API
documents an error for normal applications. Derive any `-pm` reservation from actual
decoder requirements; do not invent a fixed size or assume it grants mode B/C by itself.
Keep ordinary GXM/PAF output; introducing SceSharedFb is outside this experiment.

### 4. Coexistence has system limits

[LowMemMode](https://github.com/GrapheneCt/LowMemMode) documents Shell restrictions when
an enlarged-memory game is resident. Its source patches a firmware-specific Shell
budget check; it neither creates physical memory nor proves a usable streaming budget.
Treat it as evidence that compatibility depends on the other game and firmware, not
as a default dependency or a promise that all games can coexist with Moonlight.

## Recommended experiment, without changing the default yet

1. Add an opt-in system-mode package configuration, preserving UNSAFE, the current
   authority and host/settings storage. Use BHBB's `gdc` packaging as the reference.
   Review SFO differences separately; avoid copying every flag without knowing its effect.
2. First cap newlib, explicitly budget PAF, and add sampled budget diagnostics through
   `vita_debug_log()` only. Record startup, Settings/IME, connection, decoder setup,
   stream stop and return. No per-frame or file logs.
3. For the **frontend-only** trial, the VitaSDK counterpart of BHBB's boot fields is
   `vita_create_self(... UNSAFE MEMSIZE 32768 ATTRIBUTE 0x08)`. This is an illustrative
   configuration, not an applied patch or a streaming-ready recommendation.
4. Inspect the newly generated SELF and SFO offline. Install the complete VPK normally
   so changed launch metadata is promoted; replacing only a deployed SELF may leave
   Shell metadata from the old package. Do not edit `app.db` manually.
5. Launch from LiveArea with a normal-memory Vita game suspended. Verify Main, discovery,
   pairing, persistent Settings and native dialogs, then resume the same game session.
6. Only after that succeeds, tune the measured total/physical/graphics budgets and
   validate streaming, audio ownership, PS capture, Quick Actions, suspend/resume,
   host/network loss, repeated reconnect and orderly cleanup. Repeat with enlarged-memory
   games and record firmware/plugins and the exact package revision separately.

If adequate physical/CDRAM resources cannot coexist, the fallback design is a small
system-mode browser/launcher plus a separate streaming executable. This adds launch and
lifecycle complexity and can still require closing the Vita game when streaming starts;
it does not satisfy full coexistence by itself. Prefer proving the single-process
system-mode option before considering that split.

## Verification status of the initial investigation

Completed: reference source comparison, installed helper/header inspection, read-only
SELF/SFO decoding, ELF symbol/disassembly inspection, and framebuffer-size calculation.
No application/resource compilation, packaging, deployment, or hardware test was run.
No runtime or packaging settings were changed. All coexistence/streaming acceptance
checks remain pending; this note records feasibility and blockers only.

## Prototype on `codex/system-app-mode`

The follow-up implements an opt-in `MOONLIGHT_SYSTEM_APP` CMake option, default OFF.
Maintainer command: `bash build.sh -DMOONLIGHT_SYSTEM_APP=ON`. The build script forwards
its arguments to CMake; use `-DMOONLIGHT_SYSTEM_APP=OFF` explicitly to clear a cached ON.
Both profiles retain the same title ID, host identity/storage, AppSettings and authority.
Installing the experiment updates the existing Moonlight installation, not a second app.

The system profile uses `CATEGORY=gdc`, a nonzero SELF budget and boot `ATTRIBUTE=0x08`.
SFO `ATTRIBUTE=33472` and `ATTRIBUTE_MINOR=16` are deliberately preserved from Moonlight
to isolate those changes. This is not a byte-for-byte copy of BHBB's SFO.

The initial CMake cache parameters, all in KiB, are:

- `MOONLIGHT_SYSTEM_MEMSIZE_KIB=32768`: total SELF budget.
- `MOONLIGHT_SYSTEM_PHYCONT_KIB=0`: physical reservation **inside** that total; zero
  initially avoids guessing decoder requirements and does not establish streaming support.
- `MOONLIGHT_SYSTEM_NEWLIB_KIB=6144`: strong C-linkage `_newlib_heap_size_user` override.
- `MOONLIGHT_SYSTEM_PAF_KIB=10240`: global PAF heap supplied when loading the module.

Values are checked as nonnegative integers, within the signer's documented range and
aligned to 4 KiB. Total must be at least 4096 KiB, heaps at least 1024 KiB, and heaps
plus physical reservation must leave space inside total. Passing these checks only
establishes configuration consistency; it does not prove sufficient runtime memory or
firmware-specific physical-budget alignment. The experimental values need measurement.

PAF application mode is set after `SampleInit()`, initially with 8 MiB surface and
4 MiB text-surface pools, while retaining `graphics_option=7`. The startup-failure
follow-up below reduces those pools to 4/1 MiB. The ordinary profile keeps
its previous heap/framework/SELF settings. The newlib startup probe catches a wholly
unavailable heap and returns module-start failure with an existing-style bootstrap
console error; later allocation pressure still uses the existing failure paths.

`[Memory]` snapshots report configured capacities, actual AppMgr mode, free/total
USER_RW, PHYCONT, CDRAM and extra memory in KiB. Newlib `mallinfo()` fields separately
report allocator arena/used/free bytes; they do not describe PAF usage or the entire
process. A failed budget query is logged without interpreting the zeroed structure.
Snapshots are at application/connection/settings transitions, stream setup/stop,
decoder allocation/failure and deferred framebuffer release. No timer, worker or
per-frame diagnostic was added. Snapshot output uses `vita_debug_log()` and follows
the Debug log preference; early startup before settings initialization stays quiet.

First acceptance run: install the full VPK, keep a normal-memory Vita game suspended,
open Moonlight, open Settings and enable Debug log, close Settings, exercise discovery
and return to the same game. Capture `[Memory] settings-applied` / `settings-closed`.
Then attempt a stream and capture `[Video] decoder frameMemSize=...`, if available,
and `[Memory] video-before-physical-allocation` or the preceding failure. Use those
results to choose a physical reservation and larger total if required; do not assume
the default zero-reservation frontend trial supports AVC. A positive `-pm` request
alone still does not prove the necessary mode or CDRAM budget on the target firmware.

Checks performed: isolated execution of the actual CMake option/budget/SELF branches
in **script mode**, covering ordinary/system/custom profiles and invalid values;
source grammar checks with installed SDK declarations, `bash -n build.sh`, and diff
review/whitespace checks. No project configure, application/resource compilation,
packaging, deployment or hardware test was performed. The previous build artifacts
were not regenerated; they still describe the ordinary profile from the investigation.

### Follow-up: a successful launch was an ordinary build

The user reported a successful launch with the game-close prompt and no `[Memory]`
output. Read-only inspection of the newly available local package found source
revision `150c538d1aa8`, `MOONLIGHT_SYSTEM_APP:BOOL=OFF`, SFO `CATEGORY=gd`, SELF
type-6 attribute/physical/total all zero, and no `[Memory]` diagnostics in the ELF.
These facts explain the local package's behavior. The installed package was not read,
so they do not prove the exact device package or which build command the user ran.
The user subsequently confirmed running `build.sh` without arguments, consistent
with the ordinary profile observed locally. This was not a system-mode acceptance run.

The experiment remains opt-in: being on this branch is insufficient. The development
wrapper removed `build/` and called `build.sh` without forwarding arguments, resetting
the option to OFF even if the caller supplied ON. It now forwards `"$@"`; if the built
cache selects the system profile, it reports the VPK path and skips SELF-only deployment.
Run `bash build.sh -DMOONLIGHT_SYSTEM_APP=ON` for the simplest installation path,
install the complete VPK with VitaShell, then launch from LiveArea. The development
wrapper still has its existing Vita-IP/CLI prerequisites and needs ON on each clean run.

CMake prints `Moonlight launch profile: SYSTEM` or `NORMAL`. Applying Settings with
Debug log enabled now always emits `[Launch] profile=system system_app=ON commit=...`
or `[Launch] profile=normal system_app=OFF commit=...`. Only the system profile adds
the detailed `[Memory]` snapshots. No logger toggle is bypassed, and no streaming or
memory flags were changed in response to an unconfirmed system-profile test.

Follow-up non-build checks passed: shell syntax, exact forwarding of option arguments,
isolated deployment-branch behavior for ON/OFF and common boolean spellings, rejection
of a missing system VPK, isolated CMake profile messages and diff whitespace. Only the
relevant shell snippets and CMake script-mode blocks were exercised; no build/deploy
script, project configure, compilation, packaging or device test was executed.

### Follow-up: startup allocation failure with a game resident

The user reports an immediate startup crash with a Vita game left resident. The
kernel log shows an unsuccessful `0x00800000` (8 MiB) allocation in
`ddrmain: VLMP00001`, followed by a coredump. It does not identify the allocation's
caller, the firmware failure code, or establish that `0x00310000` is a free-memory
counter. A DDR-main physical-page allocation failure alone is not evidence that the
AVC decoder's separate PHYCONT reservation should be increased.

Read-only inspection of the current local VPK/cache now confirms the system profile:
`CATEGORY=gdc`, control-info-6 attribute 8, physical 0, total 32768 KiB. The ELF has
`[Memory]` strings and a strong `_newlib_heap_size_user`; its data contains a 6 MiB
override. The new crash dump is not available locally; the Desktop dump is older
than this test and was not used to diagnose it.

The first profile copied BHBB's 8/4 MiB surface/text pools but also reserved a 6 MiB
newlib heap in addition to the 10 MiB global PAF heap. Matching BHBB's pool sizes
therefore did not reproduce its memory footprint. The surface pool is an explicit
8 MiB allocation candidate, but matching sizes do not prove its ownership of the
failed request; inherited PAF/GXM settings and firmware allocations remain relevant.

The next source trial reduces surface/text pools to 4/1 MiB, a 7 MiB reduction in
their combined requested capacities. Global PAF/newlib heaps, total SELF budget,
physical reservation, rendering mode and authority are unchanged. Pool capacities
are not measured resident usage, and 4/1 MiB is not yet a verified sufficient size
for native dialogs, fonts, settings or video presentation.

Rebuild with `bash build.sh -DMOONLIGHT_SYSTEM_APP=ON`, install the full VPK and repeat
the same launch with the game resident. If startup succeeds, collect `[Launch]` and
`[Memory]` after applying Settings and test native UI before streaming. If it still
fails, use the fresh `.psp2dmp` and matching unstripped `.elf` to identify the failing
thread/call site before further pool or reservation changes. Later `[Memory]` logs
cannot diagnose a failure before the framework/settings are initialized, because
logging intentionally follows the existing Debug log preference.

Non-build validation checked the guarded pool assignments, the 7 MiB reduction,
unchanged ordinary-profile source and diff whitespace. No agent-run configure,
application/resource compilation, packaging, deployment or hardware test occurred.
