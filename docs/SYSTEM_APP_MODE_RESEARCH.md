# Launching Moonlight alongside an existing application

Research date: 2026-10-19. Moonlight baseline: `dcf273f4c3a22b06429a193544fb217567b0b0b5`.

**Current status, 2026-10-22:** the system profile is implemented on master and has scoped
user-verified coexistence/stream/reconnect/Settings/Quick Actions results on one Vita/game.
Current system defaults are 64 MiB total, PHYCONT reservation 0, newlib 6 MiB and PAF 10 MiB;
video uses explicit Nongameapp contexts and contiguous main-RAM output buffers. Direct CMake
defaults to normal mode; `build.sh` selects both profiles unless ON/OFF is explicitly supplied.
See the final ordinary-profile/default/dual-build sections below and README for current commands.
The initial 32 MiB candidate, branch names, “next module” requests and failed experiments below
are dated history, not instructions to repeat binary research. Minimum budgets, broad
compatibility/endurance and a clean dual build remain unverified.

## Result

**Initial result (2026-10-19): system application mode was the concrete candidate.** BetterHomebrewBrowser supplies
a small SELF memory budget and system-style packaging. VitaSDK already exposes the
relevant packaging controls, so changing compiler/toolchain is not a prerequisite.
At that stage this was source-supported only; later scoped coexistence results appear below.

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

### Follow-up: Main and Settings run with the smaller pools

The user reports that `7e20e940a52b` no longer crashes and Main/Settings work. The
runtime explicitly identifies `profile=system system_app=ON`. AppMgr reports:

- Application mode 2, corresponding to mode A in the installed SDK header, without
  physically contiguous memory access. PHYCONT free/total is 0/0 KiB.
- USER_RW free/total is 1632/32768 KiB before Settings, 1428/32768 KiB immediately
  after showing it, and 1632/32768 KiB after closing it. This one open/close cycle
  returns to baseline; it does not establish long-run leak freedom.
- CDRAM free/total is 0/16384 KiB. The reported 16 MiB total does not mean any of it
  is currently available for the renderer's additional framebuffers.
- Extra free/total is 0/0 KiB, although `extra_mem_allowed=1`; the permission flag
  alone does not establish an available extra-memory budget.
- Newlib's arena is 12288 bytes: 4760 used and 7528 free. This is the allocator's
  current arena, not the full 6 MiB reserved heap and not the process footprint.

The snapshot supports the reduced-pool startup/Main/Settings result. The user also
explicitly confirms that Moonlight no longer asks to close the Vita game and the
same game session resumes afterward. This verifies the frontend coexistence goal
for that device/game; it does not establish all-game or cross-firmware compatibility.
It does not validate the streaming path: the current decoder needs PHYCONT and
the three output buffers need CDRAM.
The reported mode/budgets cannot accommodate those allocations as they stand.

Do not infer a production newlib size from its idle arena or introduce an arbitrary
decoder reservation from this snapshot. A subsequent streaming-capable profile must
establish an available physical budget/mode and output-buffer memory while retaining
the game. Record decoder setup requirements and real allocation outcomes separately.
No code or budget change was made in response to this Main/Settings-only report.

### Follow-up: streaming fails on thread creation before decoder setup

The next user capture from `7e20e940a52b` reaches successful host launch, RTSP and
initialization of control/video/input transport state. It fails in stage 8, control
stream establishment. `sceKernelCreateThread` reports an allocation failure and
`PTE_OS_GENERAL_FAILURE: 80024302`; the pthread caller returns 11. The video setup
callback (`[Memory] video-before-setup`) has not run. This capture therefore proves
a thread-memory failure before AVC allocation, not a decoder memory-size result.

Before setup the snapshot has 1328 KiB USER_RW free; after the failed setup/unwind
snapshot it has 292 KiB. Newlib's arena changes from 217088 to 229376 bytes. Those
small arena counters do not represent kernel thread-stack allocations. The snapshot
also precedes exit of the session owner and possible deferred runtime cleanup; one
sample is insufficient to conclude that thread stacks leaked. The kernel allocation
messages alone do not identify which control thread failed.

The checked common-c `Platform.c` requests `0x100000` stack bytes for every POSIX
transport thread. `startControlStream()` creates ControlRecv, LossStats, ReqIdrFrame,
CtrlAsyncCb and optionally InvRefFrames. Four 1 MiB requests cannot fit the reported
1.3 MiB headroom. This is separate from the future PHYCONT/CDRAM requirements.

The source change uses a narrow, project-owned adapter:

- Only when `MOONLIGHT_SYSTEM_APP=ON`, CMake force-includes
  `moonlight/vita_transport_threads.h` into the dependency's `Platform.c` and adds
  `src/moonlight/vita_transport_threads.c` to the application.
- The header includes pthread declarations first, preserves Platform.c's early
  `_GNU_SOURCE` definition, and redirects only attribute initialization, stack-size
  setting and thread creation call sites in that one translation unit.
- The adapter sets 128 KiB stacks, verifies the resulting size before creation,
  destroys the caller's attributes after the attempt, and returns setup/create
  errors to common-c. It preserves common-c's ownership of the entry context and
  its existing failure unwind. A failed initialization cannot cause an unchecked
  fallback to a default stack.
- `[Thread] <name> stack_bytes=... create_result=...` uses the existing debug logger.
  There is no new worker, per-frame log, protocol change or dependency modification.
  Other application workers, the ordinary profile and the submodule revision stay unchanged.

The header is an integration adapter for the currently pinned Platform.c call pattern,
not a general pthread replacement. Recheck its three call sites and attribute ownership
when updating common-c. The ordinary profile continues to use the dependency directly.

For four control threads the requested stack capacity falls from 4096 to 512 KiB,
a 3584 KiB reduction. This arithmetic does not prove the stacks are sufficient for
all callback/crypto/decoder workloads, or that the whole stream fits the system
budget. No heap or SELF budget is changed. Build/install the full system-profile VPK
and check named creation results, progression beyond stage 8, subsequent allocation
errors, input, host/network loss and repeated failure/reconnect/stop. The renderer's
zero PHYCONT and unavailable CDRAM remain separate unresolved constraints.

Non-build checks: preprocessing a small call-site fixture verified the three macro
redirects and unmodified pthread declarations; the adapter implementation's C AST was
interpreted with injected init/stack-setting/stack-query/create failures and success
to check returned errors, creation gating and attribute ownership. Actual CMake
source-selection blocks ran in isolated script mode for ON/OFF; the dependency tree
and diff whitespace were checked. These are not compiler/SDK type checks. No application
compilation, project configure, packaging, deployment or hardware test was run.

The user's next live-worktree build reports AudioPing, ControlRecv, LossStats,
ReqIdrFrame and CtrlAsyncCb all created with `stack_bytes=131072 create_result=0`.
Stage 8 completes and stage 9 reaches `video-before-setup`, so the reported transport
thread-creation blocker is passed on this run. The log identifies HEAD `5940b72`
because the adapter changes were still uncommitted when the user built; the named
thread lines establish that those working-tree changes were included.

Video initialization then fails in `sceVideodecInitLibrary()` with `0x80620840`, before
the decoder memory-size query or external framebuffer allocation. VitaSDK's public
enum does not name that error (its ordinary out-of-memory code is `0x80620803`).
The snapshot is still mode A, PHYCONT 0/0, CDRAM 0/16384 and USER_RW 648/32768 KiB. These independent
resource constraints remain. The successful setup interval does not establish
128 KiB stack safety over a complete stream, failure recovery or long sessions.

### Decoder budget error and failed-setup cleanup

Further local primary-source inspection found the exact code in the Sony SDK
debugging error table (`sdk/host_tools/debugging/error_code/error_table.csv`):
`0x80620840` is `SCE_VIDEODEC_ERROR_NOT_GAME_BUDGET`. This identifies a budget-class
rejection by the standard decoder initialization API. It is not evidence that merely
increasing the physical reservation will make that API accept a system application.
The renderer now logs the symbolic name alongside its existing numeric failure.

VitaSDK's [SceAvcodecUser NID database](https://github.com/vitasdk/vita-headers/blob/master/db/360/SceAvcodecUser.yml)
and installed SceVideodec stub archive contain the following exports:

- `sceVideodecInitLibraryNongameapp` (`0x26F3C35D`),
  `sceVideodecQueryMemSizeNongameapp` (`0x2D9AE981`) and
  `sceVideodecInitLibraryWithUnmapMemNongameapp` (`0x108C1FEC`).
- `sceAvcdecQueryDecoderMemSizeNongameapp` (`0xFA072F66`) and
  `sceAvcdecCreateDecoderNongameapp` (`0x3102196C`).
- Nongameapp decode/get-picture variants, including `sceAvcdecDecodeAuNongameapp`
  (`0x867C75AD`) and `sceAvcdecDecodeGetPictureNongameapp` (`0xCF3CFC70`).

The public and available local SDK headers do not declare these signatures. Vita3K's
kernel implementations are unimplemented exports, not usable ABI evidence. Names
and import stubs alone cannot establish argument counts, structure layouts, memory
ownership or whether the entire decoder lifecycle must use matching variants.
The next step needs declarations or a decrypted/disassembled `SceAvcodecUser` module
to establish that contract, then real system-mode allocation results. No guessed
prototype, decoder substitution or extra reservation is introduced here.

Independently, failed setup leaked resources: `gs_sps_init()` and the init-info
allocation precede library initialization, but its failure jumped past their cleanup.
Common-c does not call renderer cleanup when renderer setup fails. The common failure
tail now stops the SPS parser and frees/resets init-info and the decoder input buffer;
library termination still occurs only after successful initialization. The vendored
parser destructor also omitted its owned `slice_data` object and optional `rbsp_buf`;
both are now freed. These fixes apply to either profile and do not enable streaming.

The reported newlib used-byte rise from 114760 before video setup to 516224 on failure
is consistent with retained parser allocations, but the log is not a complete leak
measurement. Check repeated failed attempts against a settled baseline on hardware.
Non-build checks interpret the actual cleanup paths and parser destructor with tracked
allocations, including early failure and repeated calls. No agent build/device test ran.

### Supplied SceAvcodecUser ELF inspection (2026-10-20)

The user supplied `avcodec_us.suprx.elf`. Read-only inspection confirms a 9784-byte
ARM little-endian EABI5 ELF with module name `SceAvcodecUser`, module NID
`0x5AA351E8` and SHA-256
`df88faafdbbe11a7baa1b992adf26c46bbf8bbc8308274462b1fc5636fcf3b8f`.
It has no section/symbol table; its executable LOAD segment is at file offset
`0xA0`, VA `0x81000000`, size `0x24F0`. The file does not establish a firmware
version merely from its module version or matching NIDs. The binary is not added
to the repository.

Module-info export offsets `0xEDC..0x103C` and import offsets `0x103C..0x1084`
(relative to that segment) locate 56 SceVideodecUser function exports and imports
from `SceAvcodec` (library NID `0xA166C96E`) and `SceCodecEngineWrapper`.
The important Nongameapp mappings are:

- InitLibrary: export NID `0x26F3C35D`, Thumb entry `0x810003A1`, calls the
  import `_sceVideodecInitLibraryNongameapp`, NID `0x3070B3ED`.
- QueryMemSize: export `0x2D9AE981`, entry `0x81000391`, calls
  `_sceVideodecQueryMemSizeNongameapp`, NID `0x8B52F56A`.
- QueryDecoderMemSize: export `0xFA072F66`, entry `0x81000379`, calls
  `_sceAvcdecQueryDecoderMemSizeNongameapp`, NID `0x6C290F95`.
- CreateDecoder: export `0x3102196C`, entry `0x81000381`, calls
  `_sceAvcdecCreateDecoderNongameapp`, NID `0x4099935D`.
- InitLibraryWithUnmapMem: export `0x108C1FEC`, entry `0x81000399`, calls
  `_sceVideodecInitLibraryWithUnmapMemNongameapp`, NID `0xE816BAE7`.

All five bodies, plus nine selected ordinary/other Nongameapp exports, are identical
in shape: save registers, call a matching import, restore/return. They do not read
arguments or structures, calculate sizes, or contain the budget check. The imported
addresses have on-disk unresolved import stubs, not the decoder implementation.
The ordinary InitLibrary similarly forwards to `_sceVideodecInitLibrary`
(`0xAE511B32`). This establishes the call boundary but not parameter types, exact
arity, structure sizes, allocation ownership or compatible decode/presentation APIs.
No API declaration or renderer change is justified from these wrappers alone.

The next implementation needed is `SceAvcodec`, identified as embedded
`avcodec.skprx.elf` in a bootimage by the original
[3.60 export research](https://gist.github.com/devnoname120/2975ef91ed5180fe8cf680424ebbde66).
Obtain a decrypted `os0:kd/bootimage.skprx` as ELF from the same device, then extract
the embedded module. The [RE tools documentation](https://github.com/TeamFAPS/PSVita-RE-tools#psp2-kernel-bootimage-extract--by-celesteblue-dev-and-zecoxao)
describes extraction of kernel-module ELFs from `bootimage.elf`; a standalone
decrypted SceAvcodec ELF is also sufficient. That implementation can show the real
Nongameapp argument accesses, size checks and budget/memory path.

Checks performed: ELF/program/module metadata decoding, export/import NID matching
against the installed VitaSDK databases, Thumb disassembly and byte-shape assertions
on the fourteen selected wrappers/import stubs. No binary execution, firmware/device
mutation, application build or hardware test occurred. Streaming remains unverified.

### Bootimage implementation inspection (2026-10-20)

The supplied `bootimage.skprx.elf` has SHA-256
`708db3136cef67bb86b146733f489d1bda0fd65f74b453cf7bdd5225b85c1843`.
An embedded ARM ELF at file offset `0x1A8584` identifies itself as SceAvcodec
(module NID `0xDD0E433A`); its extracted size is 176272 bytes and SHA-256 is
`c3e267f097ef679e8ade1865eafd46f33c23401ee2977fed0037a2eac0a9b22b`.
SceCodecEngineWrapper is also present at bootimage offset `0x9E024`.
Extraction/disassembly output stays outside the repository; no firmware code is executed.

The SceAvcodec user-boundary functions establish these ABI facts:

- `_sceVideodecInitLibraryNongameapp` at `0x810206D1` takes codec in r0 and
  init-info in r1. It copies **32 bytes**, then calls the Nongameapp implementation
  at `0x8100DB5C`. That implementation requires the AVC member's size field to be
  **20**, and reads width/height/reference-count/stream-count at offsets 4/8/12/16.
  Supply a zeroed `SceVideodecQueryInitInfo` union, not a standalone 20-byte allocation.
- QueryMemSizeNongameapp at `0x810205C9` takes codec, init-info, output in r0/r1/r2;
  input copy is 32 bytes and output is one 4-byte memory-size value.
- QueryDecoderMemSizeNongameapp at `0x81020455` takes codec, query, output;
  query copy is 12 bytes and output is 4. CreateDecoderNongameapp at `0x810204C9`
  takes codec, read/write control, query; both structures are 12 bytes, matching
  VitaSDK's SceAvcdecCtrl/SceAvcdecQueryDecoderInfo. Both creation variants reach
  the shared implementation at `0x810165EC`.
- InitLibraryWithUnmapMemNongameapp at `0x8102063D` takes codec, read/write control,
  init-info. Control copy is 20 bytes, init-info copy 32. Control matches the SDK
  layout: buffer pointer/size, UID, context address and context size (five words).
  The backend at `0x8100EA04` consumes context address/size at offsets 12/16.

Merely replacing InitLibrary with InitLibraryNongameapp cannot solve this failure.
Both eventually call `0x8100BCDC`. With no supplied context that helper calls
`ksceKernelIsGameBudget` (`0x8100BD7C`) and returns `0x80620840` on false
(`0x8100BDF8`). Decoder creation has an equivalent game-budget rejection when
frameBuf.pBuf is zero (`0x81016A58`). Both library and decoder therefore need
explicit storage in the system profile. The context requires 256 KiB alignment;
the AVC frame store requires 1 MiB alignment. Nongameapp queries accept aligned
dimensions up to 1920x1088, unlike the ordinary path's smaller pixel-count bound.

SceCodecEngineWrapper's OpenUnmapMemBlock implementation at `0x81015E84` checks
the supplied uncached range has one physical span, creates an alias and unmaps
the original block. Its allocator returns addresses in the original virtual range.
Close verifies allocations are released, restores the original mapping and frees
the alias (`0x81015D20`). Never CPU-access the arena while it is open; destroy the
decoder and terminate the library before freeing their context allocations, close
the CodecEngine pool, then free the backing memblock. The SDK's PHYCONT allocation
attribute (`0x00200000`) can request a contiguous USER_MAIN_NC_RW block without
selecting the separate reserved-PHYCONT memblock type; permission, availability and
fragmentation in this system profile still need hardware validation.

The ordinary decode entry at `0x81020BE1` uses the existing 12-byte control,
24-byte access unit and 12-byte output-array layout, then resolves the shared
decoder instance by handle. Retaining that decode/presentation API with an explicitly
backed Nongameapp-created instance is a source-based experiment, not a device result.
No claim of streaming support or all-firmware ABI compatibility follows from this dump.

### Explicit-context system renderer prototype

`src/moonlight/vita_system_video.c` isolates the recovered API declarations and
ownership; it is compiled/linked with SceCodecEngine_stub only in the system profile.
It supplies a zeroed 32-byte init union, queries library and decoder sizes, bounds
rounding arithmetic, then allocates an uncached USER_MAIN_NC_RW arena with SDK
attributes PHYCONT and HAS_ALIGNMENT. Library storage is rounded/aligned to 256 KiB,
decoder storage to 1 MiB; one extra MiB covers pool metadata/alignment padding.
The backing arena is opened as a CodecEngine pool and is never CPU-accessed while
unmapped. Its two suballocations are passed to InitLibraryWithUnmapMemNongameapp
and CreateDecoderNongameapp, avoiding their automatic game-only allocation paths.
Each query/allocation/init stage uses the existing debug logger.

System-profile output buffers use separate contiguous uncached main-RAM blocks
with the existing GXM mapping and deferred PAF-surface release. The ordinary
renderer retains its prior reserved-PHYCONT decoder and CDRAM output allocations.
Decode uses the existing public API and remains an unverified compatibility step.

Teardown deletes the decoder, terminates the library, frees both suballocations,
closes/remaps the CodecEngine pool, then frees the backing memblock. Setup failure
uses the same unwind. A failing teardown retains the remaining backing resources
and prevents a new adapter session; a renderer retaining its old decoder rejects
restart before allocating another SPS parser/input buffer. Restart the application
after such a cleanup error. The pool/context memory is never passed to GXM.

No default heap/SELF reservation is increased. The previously tested 32 MiB budget
has only 648 KiB USER_RW free before video and cannot be assumed to fit this path.
For the next measurement use the README's explicit 65536 KiB total, PHYCONT zero
command and install the full VPK. 64 MiB is a headroom experiment within VitaSDK's
documented system-budget range, not a measured minimum or proof of coexistence.
Check named query sizes/results, contiguous-arena permission/availability, all three
output allocations, GXM mapping/presentation and actual decode; then audio/input,
stop/reconnect, stable post-cleanup budgets and resuming the original game.

Non-build validation interprets the actual adapter C AST with mocked Vita APIs:
nine setup failures, zero/oversized query outputs, success, repeated teardown and
five teardown failures exercise ownership/order and retained-resource reuse guards.
Source selection/linkage and ordinary-profile source preservation are checked
separately. These checks do not replace VitaSDK compilation or device validation;
neither was run by the agent.

### First successful stream alongside a game (user validation)

The user's `8b5d9a54717c` capture confirms system profile, total 65536 KiB, physical
reservation zero and mode A. QueryMemSizeNongameapp reports 5767168 bytes (5.5 MiB)
and QueryDecoderMemSizeNongameapp reports 8388608 bytes (8 MiB) for 960x544x60.
The 15728640-byte (15 MiB) contiguous main-RAM arena opens successfully as a
CodecEngine pool. Explicit library init/create both return zero. Three 2 MiB
main-RAM output blocks map successfully; video-ready USER_RW free is 11396 KiB,
then 10048 KiB after the remaining audio/input/transport setup completes.
All eleven stages complete and the renderer reports 1088 decoded frames.

The user explicitly confirms working image, sound, controls, and resuming the
original suspended Vita game after Moonlight. This establishes the basic goal on
that device/game with this 64 MiB configuration. It does not establish a minimum
budget, cross-firmware/all-game support or long-session/reconnect stability.

After out-of-order video data at frame 249, the capture has exactly 120
`Waiting for IDR frame` messages, then `Reached consecutive drop limit`, an IDR
request, and resumed decoding. These match the pinned common-c depacketizer's
per-completed-frame wait logging and 120-frame consecutive-drop limit. It is
recovery after a stream discontinuity; the log cannot identify whether network,
scheduling, console output, or another cause produced it. The 4400 ms first-audio
arrival and audio out-of-order recovery are also observations, not decoder errors.
`connection status=0` is CONN_STATUS_OKAY in the pinned header. The control error
4 appears after the user's PS double-press stop and ENet interruption; it does
not indicate that initial connection setup failed.

Stop finishes with result zero, audio drain/release zero and no nongame cleanup
error. Before deferred framebuffer release USER_RW free is 25408 KiB; releasing
the three mapped blocks increases it by exactly 6144 KiB to 31552 KiB. All three
GXM unmaps return zero. Before setup free was 33584 KiB, so the final snapshot
still has 2032 KiB less headroom. Newlib used changes from 93056 to 3279376 bytes;
the arena grows to 3948544 bytes. Arena growth alone is not a leak, while the
retained used-byte difference needs a repeat/settled-baseline investigation.
Neither these arena counters nor their free field describe the whole reserved
6 MiB newlib heap. Do not increase that heap solely because an early arena-free
sample was small. User-run results are distinct from agent checks; the agent did
not build, deploy or run the application.

The follow-up project log callback rate-limits only the exact common-c format
strings `Waiting for IDR frame\n` and `Waiting for RFI frame\n`. Each kind emits
immediately, then at most once per second with the preceding omitted-repeat count.
After transport threads have joined, a summary preserves total message/omission
counts including a final partial interval. A mutex protects the two small counters;
console output occurs outside the lock. State resets before each session. Other
messages, IDR requests, drop limits, transport sources and renderer behavior remain
unchanged. The count describes log messages, not a new network-loss metric.

Non-build checks interpret the actual timing-helper C AST: 120 messages at 60 Hz
yield two periodic emissions and 118 omissions; tests cover the exact one-second
boundary, final pending counts, independent IDR/RFI state and session reset.
Source checks cover callback pass-through, logging outside the lock, teardown
summary ordering and an unchanged common-c tree. Build and hardware validation of
this diagnostic change remain pending; it is not claimed to fix the initial loss.

### Longer run: newlib allocation failure (2026-10-20)

The next user capture shows normal decoded-frame progress until 2732 frames,
then `Video Receive: malloc() failed`. The asynchronous termination worker starts
successfully and requests cleanup with result -1. The control-stream error 4
appears during teardown, after video allocation failure, so it is not the trigger
in this ordered capture. This is neither an initial nongame decoder-budget error
nor a PS double-press stop.

The 65536 KiB total / 6144 KiB newlib configuration remains active. After transport
stop, newlib reports arena 6287360, used 5592368, free 694992 bytes. Deferred
framebuffer release succeeds and increases USER_RW free from 25268 to 31412 KiB;
newlib used then remains 5591728 bytes. Free main RAM does not automatically
extend the separate newlib heap cap. Compared with the previous shorter run,
the retained used-byte count is higher. These samples motivate allocation-owner
and fragmentation diagnostics; they do not identify a leak site or distinguish
all persistent allocations from leaked ones. The failed request size is missing.

Source review of video receive/FEC/depacketizer handoff, direct decode completion,
renderer buffers, SPS parsing and transport cleanup did not establish a specific
missing free. Rather than change a dependency or raise the heap without that
evidence, `MOONLIGHT_HEAP_TRACE=ON` enables linker wrappers for malloc, calloc,
realloc and free. This is an optional system-profile-only diagnostic; it defaults
OFF and leaves the 6 MiB newlib cap intact. Fixed BSS metadata holds 8192 live
entries, 1024 hash buckets and 256 call sites (about 136 KiB on Vita); site zero
aggregates overflow. Pointer records use chained hash buckets and a reusable
free list. Metadata updates never call the allocator or print, and real allocator
calls occur outside the lock. Contention yields with `sceKernelDelayThread(1)`
to allow a preempted lower-priority lock owner to finish.

Snapshots before/after setup, on video receive OOM and after stop print tracked
live totals, twelve largest live sites, peaks, the last allocation-failure size
and caller, and coverage counters. Linker wrapping does not cover every libc
internal allocation/free; dropped entries, unknown frees, replaced pointers and
NULL zero-size realloc results explicitly flag limitations. These totals must
be compared with `mallinfo`, not equated to it. No per-frame or per-allocation
console/file output is added. Diagnostic CPU overhead and stack usage need Vita
validation; capturing it is not proof that the uninstrumented stream is stable.

Keep the exact diagnostic build's unstripped ELF. For an ASLR-adjusted caller,
subtract logged runtime `anchor_malloc` and add the ELF symbol address of
`__wrap_malloc`, using consistent Thumb-bit treatment. Resolve the resulting
return address with VitaSDK addr2line/objdump; inspect the preceding call when
the returned line is the instruction after it. The ELF must match the VPK.

Non-build checks interpret the actual tracer C AST: hash collisions, entry/site
saturation, free-slot reuse, null/overflow allocation, realloc failure restoring
old ownership, in-place/moving/zero-size realloc, pointer-reuse interleaving and
snapshot preservation. They verify allocator/logging calls are outside the lock.
Isolated CMake script checks exercise ON/OFF source/link selection and reject
tracing with the ordinary profile; they do not configure the application.
No application/resource compilation, packaging, deployment or hardware test was
run by the agent. Next evidence needed: the diagnostic `[Heap]` snapshots and
matching ELF; the underlying longer-session failure is still open.

### Diagnostic linker-flag correction

The user's diagnostic build of `18a3264` failed at link. Its generated `link.txt`
has no `--wrap` switches even though `heap_trace.c.obj` is present and `nm -u`
shows `__real_malloc`, `__real_calloc`, `__real_realloc` and `__real_free` references.
Source inspection identifies the ordering error: the trace block appends wrapping
flags, then a later `set_target_properties(LINK_FLAGS ...)` replaces the property
with only `-nostartfiles -nostdlib`. The startup-flags block now also appends.
The compile-property setting and all ordinary-profile options retain their values.

The earlier isolated selection check inspected only the diagnostic block and
missed this later overwrite. Follow-up CMake script-mode checks exercise the full
sequence from that block through the startup flags, using real GLOBAL string
properties in place of target properties without configuring any project. They
reproduce the overwrite in the prior source and verify preservation of all four
wrappers, startup flags and any earlier flags after the fix, with tracing ON/OFF.
Inspection/checks are distinct from relinking: no build/link command was executed
by the agent, and the maintainer's successful build and Vita trace remain pending.

### Diagnostic run: skipped pthread initialization and ENet OOM abort

The user's next log identifies `df0792aa9951`, with tracing enabled and the same
65536/6144 KiB total/newlib budgets. It advances through all eleven setup stages
and video decoding. Before setup the trace contains 3731 blocks / 75143 bytes;
after setup it contains 4206 / 664152. At video OOM it reports a failed 1088-byte
request at runtime caller `0x8107f8e5`. The trace table is full (8192 entries,
82007 dropped insertions, 53597 unknown frees). Its 1303038 tracked live bytes
cannot be used as the complete heap usage. There are no replaced-pointer or
zero-realloc events in this capture.

Read-only inspection uses the matching local unstripped ELF:

- ELF SHA-256: `e058bddace01772f2394891e9d114fdd158d284468e5f6a20313696be3ba4bdf`.
- Supplied compressed dump SHA-256: `59c515143182de136dee1090c5f6b9313516b8b54e990700b768b8b12f6aa6a9`.
- ELF `__wrap_malloc` is `0x8102a770`, runtime anchor is `0x81086771`.
  Clear the Thumb bit before subtracting: RX relocation delta is `0x5c000`.
- Failure return address maps to `VideoReceiveThreadProc`, VideoStream.c:129.
- Runtime `0x8126225f` maps to return address `0x8120625e`, `pte_new` immediately
  after `calloc(1, 160)`. This site holds 4048 tracked blocks / 647680 bytes at
  failure, versus three blocks before setup. Overflow means this is a lower bound.
- The numerous approximately 40 KiB sites map to the SPS parser's one-time tables;
  their captured counts remain unchanged. OpenSSL's default allocator has thousands
  of much smaller blocks, but this capture's dominant growing tracked site is PTE.

Dump module RX base is `0x8105c000`; RW base is `0x81380000` and has a different
relocation delta. Do not apply the RX delta to globals. The supplied dump contains
thread/stack data but does not include the PTE globals' memory, so their runtime
values were not read or assumed to be observed. The failed thread PC is
`0x8126fd50` (`_kill_r`, deliberate undefined instruction). Its stack resolves to
`abort` -> `enet_malloc` (callbacks.c:46), outgoing ENet control queueing and
encrypted control sending. ENet's default no-memory callback is `abort`. The earlier
video OOM and failed AsyncTerm creation are ordered in the log before this abort;
the dump does not indicate a decoder fault or prove a thread stack overflow.

The local ELF has a `.init_array` entry `0x81000021`, `pthread_setup`, whose only
call initializes pthread. There is no other call to `pthread_init` in the old
executable. The custom module_start manually initializes newlib but never runs
that constructor. The linked `pthread_self` calls `pte_new` whenever the self TLS
key has no value; with an uninitialized NULL key, pthread_getspecific returns
NULL and pthread_setspecific cannot retain the identity. Repeated pthread API
use therefore accumulates 160-byte thread records, consistent with this trace.
The public SDK header declares the required explicit lifecycle functions. Upstream
[pthread_init.c](https://github.com/vitasdk/pthread-embedded/blob/master/pthread_init.c)
also documents explicit initialization, creates the self/cleanup keys and returns
nonzero on success; the local machine code confirms those semantics.

The bootstrap now calls pthread_init after newlib and the system heap probe,
before entering PAF/application code, in both profiles. Initialization failure,
NULL identity or unequal successive pthread_self results stop startup and unwind
newlib. A successful initialization is paired with pthread_terminate after app
shutdown and before newlib teardown, including later startup-failure cleanup.
It does not run unrelated constructors, alter vendored protocol/ENet/PTE sources,
increase heap capacity or suppress the ENet OOM abort. Bootstrap errors use the
existing early console error path; allocation tracing remains opt-in.

Non-build checks interpret the actual bootstrap C AST for ordinary/system paths:
initialization and teardown order, nonzero pthread-init success values, init
failure, NULL/changing identity, PAF module load failure and heap-probe failure.
ELF symbol/disassembly checks and dump parsing are read-only. No compilation,
linking, packaging or hardware test was run by the agent. The maintainer should
repeat a longer stream and reconnects with tracing, retaining the same 6 MiB heap,
then compare settled before/after snapshots and confirm the suspended game resumes.
The source cause is addressed; sustained runtime stability remains unverified.

### User retest: two longer streams and stable sampled memory

The user confirms streaming no longer exits and restarting the stream also works.
The capture contains two complete sessions at 960x544x60 / 10000 kbps, with system
budget 65536 KiB, physical reservation zero, newlib 6144 KiB and PAF 10240 KiB.
The local build flags identify `922f1e92f9d7` with tracing enabled; the capture
starts after connection to psp2shell and has no new Launch line. ELF wrapper
address `0x8102a800` and runtime anchor `0x81071801` give RX delta `0x47000`.
Runtime caller `0x8124d2ef` maps to `pte_new` in this matching local ELF.

The first session decodes 22270 frames and the second 18008 (40278 total).
Both end after the input log's explicit PS double press; stop workers return zero.
All eleven setup stages complete twice. Audio drain/release and all six deferred
GXM unmaps return zero. Control error 4 follows the requested stop and ENet wait
interruption in each session; it is not an unsolicited connection failure.
There is no video malloc failure, coredump or failed thread creation in the log.
No wall-clock duration is recorded, so decoded counts are not exact elapsed times.

Before the first session newlib arena/used is 229376/93104 bytes. After setup,
used is 678072 bytes. After the first stop it is 97496, then 96768 after deferred
frame release. The second setup uses 679912 bytes; after stop it is 97672, then
96936 after frame release. Arena stabilizes at 704512 bytes. The two corresponding
final USER_RW-free samples are both 32952 KiB; the post-setup samples are both
10184 KiB. The first run retains 768 KiB more main RAM than the initial pre-stream
sample, but the second final sample does not decline further. Do not attribute
that initial difference to a particular cache without allocation evidence.

The PTE call site starts with three blocks / 480 bytes and reaches fifteen / 2400
bytes, with peak equal to 2400 at both stop snapshots and the same count before
the reconnect. This verifies that the previous per-call growth into thousands of
records is absent in these sessions. All sampled trace failure, dropped-entry,
replaced-pointer and zero-realloc counters are zero. Unknown-free counts are
41 initially, 127 after the first stop and 213 after the second, so libc-internal
coverage limitations still apply. Tracked live totals after stops are 78430 and
78411 bytes; `mallinfo` and wrapper totals remain distinct measurements.

Final newlib used differs by only +168 bytes between sessions. This is evidence
against the prior multi-megabyte retained growth, not proof of zero leakage over
arbitrary reconnect counts. Two longer sessions and one reconnect are now user-
verified on this device/configuration. Longer endurance, more cycles, broader
firmware/game compatibility and the full native-UI matrix remain open.

The first session also logs system overlay activation, release of host controller
state, overlay return and PS suppression clearing while decoding continues.
Five single-frame network-drop messages are followed by recovery and continued
decode; the user reports no crash. These logs alone do not establish network
quality or visually verify every Quick Actions behavior. No new confirmation of
returning to the suspended Vita game is present in this particular report; the
earlier explicit coexistence confirmation remains the evidence for that path.

Only documentation/checklist status changes follow this retest. The agent parsed
the supplied log, checked counters/order and resolved the PTE caller in the local
ELF; it did not build or run the application. Heap tracing can now be disabled
with `-DMOONLIGHT_HEAP_TRACE=OFF` while retaining the tested budget options.

### User confirmation with heap tracing disabled

After the explicit build command with system mode ON, total 65536 KiB, physical
reservation zero, newlib 6144 KiB and heap tracing OFF, the user reports all
requested checks passed: streaming, reconnect, Settings, Quick Actions and return
to the resident Vita game. This extends user validation to the non-tracing build
on the same device. There is no new log or timing/heap measurement in this reply;
the quantitative evidence remains the preceding traced run. The ordinary profile
still needs a regression smoke test of the shared pthread-bootstrap change.
No runtime source/budget defaults change in this documentation update, and no
agent build or hardware test was run.

### Ordinary-profile confirmation and validated system default

The user next reports all ordinary-profile smoke checks passed with system mode
OFF and heap tracing OFF: launch, Settings, streaming and reconnect. No new log
or quantitative memory measurement accompanies that confirmation. This verifies
the shared pthread-bootstrap change on both launch profiles on the user's Vita.

Fresh system-profile configurations now default to total 65536 KiB, retaining
physical reservation zero, newlib 6144 KiB and PAF 10240 KiB. This selects the
already-tested configuration when enabling `MOONLIGHT_SYSTEM_APP`; it does not
establish a minimum budget. System mode and heap tracing still default OFF.
Existing cached budget choices are not forced to new values: an old 32768 KiB
cache needs an explicit `-DMOONLIGHT_SYSTEM_MEMSIZE_KIB=65536` override.

Isolated CMake script-mode checks exercise the actual option/budget and SELF
selection blocks: fresh ordinary/system defaults, retained explicit overrides,
invalid-budget rejection and absence of system memory/physical flags in ordinary
mode. No project configuration, compilation, linking or packaging is run by the
agent. The runtime budget value has user-run device validation; the new default
selection has non-build checks. Broader compatibility/endurance remains open.

### Dual-profile build-script workflow

At the user's request, `build.sh` now builds both profiles by default using
separate `build/normal/` and `build/system/` directories. It compiles the shared
CXML once, copies the RCO to both build trees and publishes profile-suffixed
VPK/SELF/unstripped ELF files under `build/` after both builds succeed. Existing
cached profile switches cannot change that selection. Explicit command-line
MOONLIGHT_SYSTEM_APP ON/OFF still selects one profile in the legacy root build
tree, with generic paths preserved and named artifact copies added. Development
deployment passes OFF before user arguments, allowing a later ON override.

The script defaults heap tracing OFF for each invocation. An explicit trace ON
in a dual build applies only to the system half. Other CMake arguments retain
their quoting and are forwarded to the selected profile(s). Both VPKs have the
same title ID, so they are alternatives for installation rather than separate
installed apps. Syntax and isolated shell-helper checks use stub configure/build
functions and synthetic files in temporary directories; no real build entry point,
resource compiler, project configure or packaging runs during agent validation.
The maintainer must validate the new dual invocation and its resulting packages.
