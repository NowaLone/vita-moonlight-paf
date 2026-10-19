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

## Verification status

Completed: reference source comparison, installed helper/header inspection, read-only
SELF/SFO decoding, ELF symbol/disassembly inspection, and framebuffer-size calculation.
No application/resource compilation, packaging, deployment, or hardware test was run.
No runtime or packaging settings were changed. All coexistence/streaming acceptance
checks remain pending; this note records feasibility and blockers only.
