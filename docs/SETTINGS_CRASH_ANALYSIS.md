> Historical investigation record. Temporary Settings/FIOS probes, screen report,
> diagnostic build options and forced console logging were removed after the
> successful device checks. The diagnostic build commands/artifact paths below
> describe past experiments and no longer apply to current source. The authority
> correction and native AppSettings path remain in place.
>
> Current status, 2026-10-22: the reported Settings regression and requested PS/Quick Actions/
> reconnect checks were user-verified after the authority correction. The closure/cleanup
> sections supersede the early “next diagnostic” requests and lack-of-runtime-evidence
> statements. API inventory/source review is now complete; current work order is in ROADMAP.

# Settings crash: full-dump analysis (2026-10-19)

## Scope and evidence

The user reports a freeze followed by C2-12828-1 after selecting Settings on
Main. Saved PCs appear on startup in the reported run. This document records
initial offline findings. That dump analysis alone did not establish a fix; later sections
record the authority correction, successful device checks and diagnostic removal.

Input: `psp2core-1792359321-0x000007394b-eboot.bin.psp2dmp`, SHA-256
`f4fef97599cdc563e4d41e26db45610ea69e00e2d34bab600a6369bf1ece70f2`.
The gzip payload is an ARM ELF32 core with 68 program headers. The original
dump and extracted process memory are not repository artifacts.

The comparison ELF is the local baseline build at source HEAD `761d730f53a3`,
SHA-256 `a175ec0b8745c358133e553652dd7e5a84ba58f078be61ee7b9456f809ce8cef`.
The user has not confirmed that this is the exact ELF of the installed VPK.
Application symbol mappings below are therefore conditional on that match.

## Fault and reconstructed call chain

The fault is a prefetch abort with PC and IFAR zero. LR is `0xe025fc89`,
within ScePaf RX `+0x58919`. The reported stack peak, 2168 of 10192 bytes,
does not indicate a stack overflow. `0xdeadbeef` in R1 is insufficient to
diagnose a dangling callback or heap corruption.

The core includes ScePaf exports and ARM unwind metadata, even though it
does not include the executable instructions at the fault's LR site. The
constructor entry is `0xe025f873` (Thumb), export NID `0x32597EF6`, mapped by
the installed PAF SDK stub to `paf::Plugin::Plugin(InitParam const*)`.

Unwinding the captured stack using the compact unwind entries gives:

1. `0xe025fc89`: `paf::Plugin::Plugin(InitParam const*)`.
2. `0xe025d767`: internal PAF frame, function entry `0xe025d630`.
3. `0xe025e453`: `paf::Plugin::LoadSync(...)`, entry `0xe025e43d` (Thumb).
4. `0x81077e11`: application return address.

Reproduction details for the PAF portion: captured EXIDX range
`0xe0482ed4..0xe049c91c`; the three compact entries are respectively
`0x80b246af`, `0x8000afb0`, `0x80a8b0b0`. Starting SP is `0x81420788`;
the recovered frame SPs are `0x81420ac8`, `0x81420af0`, `0x81420af8`.
Use ARM EHABI PREL31 decoding and Thumb-address normalization when resolving
these entries. The GUI's additional reported address `0xe03671a1` is not
part of this recovered caller sequence; do not use it alone as a caller.

Application RX starts at `0x81069000` in the dump, versus `0x81000000` in
the comparison ELF. Subtracting the relocation delta `0x69000` maps the
recovered application return to `0x8100ee11`. `arm-vita-eabi-addr2line`
maps it to `moonlight_settings_init`, and disassembly shows the immediately
preceding instruction at `0x8100ee0c` calls `paf::Plugin::LoadSync`.

This strongly localizes this crash to AppSettings plugin construction,
before the application's subsequent XML lookup, `AppSettings::GetInstance`
and `Interface::Show`. Changing the Show callback table's stack lifetime
would not address the observed failing stage.

## Module and callback evidence

The parsed MODULE_INFO records enumerate 18 registered modules. ScePaf,
ScebXCe, SceIniFileProcessor and SceCommonGuiDialog are present. There is no
registered AppSettings module or AppSettings export library in these records.
This supports investigating module load/start failure; it does not identify
the loader return code or exclude a module that was unloaded after failure.

The captured Plugin InitParam and Plugin object contain the expected names
and paths:

- `app_settings_plugin`, caller `__main__`;
- `vs0:vsh/common/app_settings_plugin.rco`;
- `vs0:vsh/common/app_settings.suprx`;
- lifecycle callback pointers `0x81285010` through `0x81285050`.

R2/R12 equal the first callback address. In the comparison ELF this maps
exactly to the `sce::AppSettings::PluginSetParamCB` import stub, library NID
`0x9522D063`, function NID `0xB3935DD3`. This is consistent with the failing
construction stage, but the runtime stub instructions and the PAF call-site
instructions are absent from captured PT_LOAD ranges. A register match
cannot by itself prove the actual indirect branch target or import binding.

The existing initialization pattern matches the local NetStream reference,
`NetStream/source/menus/menu_settings.cpp`: same native module/resource,
same five AppSettings callbacks, and `Plugin::LoadSync`. NetStream initializes
settings before its interactive UI; this timing difference is an observation,
not an established explanation for this failure.

## Checks and limitations

- The installed `vita-crashdump` CLI processed the supplied dump with the
  comparison ELF. Its ordinary stack output remained identical to the GUI.
  That result does not mean the named ELF copy is invalid: this crash's
  immediately visible addresses are in a system module.
- Custom offline ELF-note, module/export and ARM compact-unwind inspection
  recovered the additional call chain above. Local SDK export stubs and
  application `addr2line`/disassembly were cross-checked.
- No useful AppSettings loader error was found in the captured TTY notes.
- A read-only live psp2shell query did not yield module data; the local CLI
  failed on disconnect. FTP access exposed ux0/ur0 but denied the requested
  vs0 directory. No console files were changed or modules started.
- No application runtime source, authorization ID, storage format or Main
  refresh code was changed for this investigation. No crash fix is claimed.

## Next diagnostic step

Obtain the module load/start result or the matching firmware PAF instructions
around RX `+0x58919` and the runtime AppSettings import stub. Confirm the
installed VPK/ELF pair and firmware/CFW first so those addresses can be
interpreted reliably. Any instrumentation should be isolated to the native
settings adapter, preserve console-only logging policy and avoid requiring
the user to open the crashing Settings page to enable diagnostics.

Do not switch authorization IDs, replace Sony Settings, move initialization
timing, or patch Main refresh based only on these incomplete loader details.

## Device follow-up: ioPlus and isolated category test (2026-10-19)

The user identified the device as firmware **3.60 with HENkaku** and reported
that restoring **ioPlus** resolved the problem. They also reported that
NetStream and BetterHomebrewBrowser could open Settings without ioPlus.
The exact installed packages and loaded ioPlus version were not captured.
The earlier ux0-to-uma0 observation has not been independently retested or
explained by a captured mkdir error code. Do not infer corruption of settings
or a fixed filesystem path from successful Settings opening alone.

This changes the next investigation from an unexplained loader crash to a
reproducible dependency on the device's I/O environment. The reviewed
[ioPlus 0.1 implementation](https://github.com/TeamFAPS/PSVita-RE-tools/blob/master/ioPlus/ioPlus-0.1/src/ioplus.c)
hooks FIOS overlay creation and replaces an `invalid:` source with its
destination. This supports investigating filesystem restrictions, but does
not prove the exact denied path or policy in the user's installed version.

Reference packaging differs from ours:

- NetStream `f812de87f739`, `NetStream/param.sfx`, uses `CATEGORY=gdc`.
- BetterHomebrewBrowser `9e615e9186bf`, `param.sfx`, also uses `CATEGORY=gdc`;
  its CMake signs with authority ID `0x2200000000000001`.
- Our baseline at `ea0595c16640` uses the VitaSDK default `CATEGORY=gd` and
  authority ID `0x2F00000000000101`. The native AppSettings LoadSync pattern
  matches both references. Category and authority ID are separate variables.

### Change under test

The user authorized an isolated `CATEGORY=gd` to `gdc` experiment. The only
build behavior change is `-s CATEGORY=gdc` in `VITA_MKSFOEX_FLAGS`.
Authority ID, SELF generation flags (`UNSAFE ATTRIBUTE 0x0E`), SFO attributes,
runtime code, initialization timing and storage formats/paths stay identical.
This is a diagnostic packaging hypothesis, not a proven ioPlus-free fix.

### Local validation

- `bash build.sh` completed successfully using the existing build directory,
  including CXML, ELF, SELF and VPK generation. This was not a clean build.
- Both VPK archives passed ZIP integrity checks. Their entry names match,
  and the only entry with different content is `sce_sys/param.sfo`.
- Parsed SFO values differ only at `CATEGORY` (`gd` versus `gdc`). Both keep
  title ID `VLMP00001`, `ATTRIBUTE=0x82c0`, `ATTRIBUTE_MINOR=0x10`.
- Packaged `eboot.bin` and the comparison/debug ELF are byte-identical
  between the variants. Parsed SELF authority ID is `0x2F00000000000101`.
- The gd control artifacts are preserved under `build/category-test/gd/`;
  the gdc artifacts under `build/category-test/gdc/`. The ordinary
  `build/vita_moonlight_paf.vpk` is the gdc test build. All are ignored output.
- `build/category-test/manifest.json` records hashes and pending hardware
  status. Control VPK SHA-256:
  `de980c13e035b4e0bbb4fdb191dcd940f1719cc48f6508746c76e04181a7d310`.
  Test VPK SHA-256:
  `3e678b233d03abe60bc647868f7363c21171cb3d9485b2817aedbe4a0d9474c1`.
  Both embed source revision `ea0595c16640`; they were built before committing
  the packaging experiment, so the About revision alone cannot distinguish
  them. Identify the VPK by its hash.
- No deployment, installation, plugin change or new Vita test was performed
  by the agent.

### Original category-test procedure

1. Keep backups of the existing savedata and both moonlight data directories;
   do not clear or migrate them for this experiment. Install the gdc test VPK
   over our existing application. Keep other plugins/configuration unchanged.
2. Disable only the existing ioPlus entry, fully reboot and ensure HENkaku
   is active. Record which VPK was installed and the plugin configuration.
3. Open Settings, change a visible setting, close the application completely,
   relaunch and verify the value persists. Confirm the normal Main UI works.
4. Check which moonlight directory the application actually uses for a new
   saved-PC operation and whether it persists after relaunch. Existing files
   or a write performed by VitaShell do not prove this application's access.
5. If the basic checks pass, exercise stream start/stop/reconnect, audio/video,
   input and PS Button/Quick Actions to catch changes in shell behavior.

If Settings still crashes, restore the original ioPlus configuration and
reboot; record the result without changing another packaging parameter at
the same time. If it succeeds, report category as verified on this one
firmware/device only after the persistence checks. Cross-firmware behavior
and the precise firmware policy remain unverified.

## Negative category result and targeted diagnostics (2026-10-19)

The user reports that after reinstalling the test application and disabling
ioPlus, the problem recurred. This is a negative result for category alone;
it is not evidence that the entire native Settings architecture is wrong.
No new dump, package hash or filesystem/loader return codes accompanied
this report. Persistence and streaming tests could not be completed through
the crashing Settings flow.

The category experiment is therefore removed from the normal configuration.
`CATEGORY=gd` is explicit: VitaSDK stores `VITA_MKSFOEX_FLAGS` in CMakeCache,
so merely removing `-s CATEGORY=gdc` from source left gdc in an existing
build directory during validation. The explicit final gd value overrides
that inherited flag; both final packaged SFOs were inspected.

Review of [HENkaku's parse_headers_patched](https://github.com/henkaku/henkaku/blob/master/plugin/kernel.c)
shows that, when unsafe homebrew is allowed, both its standard unsafe-ID
branch and its custom-ID branch grant extended permissions; the latter also
copies the requested authority ID. This does not identify the resulting
firmware filesystem overlays or demonstrate that our custom ID is wrong.
No authority ID, SAFE mode or alternate AppSettings loader is being tried.

### Diagnostic scope

`MOONLIGHT_SETTINGS_DIAGNOSTICS` is an explicit CMake option, default OFF.
The normal Debug log toggle remains default-off. When this build option is
enabled, **only** messages beginning `[SettingsDiag] ` are allowed through
`vita_debug_log` independently of that toggle, because loading Settings must
work before its toggle could normally be applied. Other logs retain their
existing toggle behavior. All output is console-only, with no per-frame work.

The diagnostic build records:

- Real `sceIoMkdir` return values for the existing startup host-path attempts
  and the path selected. It does not add directory creation or change fallback
  decisions. `0x80010011` (already exists) remains an accepted result.
- `sceIoGetstat`, read-only `sceIoOpen`, four-byte read and close results for
  `vs0:vsh/common/app_settings.suprx` and `app_settings_plugin.rco`, before
  the original plugin load. It does not modify either system file.
- Four instruction words of the existing PluginSetParamCB import stub, before
  LoadSync and after it returns. The earlier dump omitted this code memory.
- Literal BXCE, INI processor and Common GUI Dialog load return values, using
  the same calls in the same order as before.
- Whether the original LoadSync returned and whether Find sees the plugin.

This does not add an independent load/start call for AppSettings or catch
an exception inside PAF. A missing final line is consistent with failure
within LoadSync; the dependency return codes are not the AppSettings loader
return code. Successful user-mode file access does not prove loader approval,
start success or import rebinding. Read probes and output may alter timing,
so a changed symptom must be recorded rather than called a fix.

### Reproduce the diagnostic build

Run from the repository root with the normal VitaSDK environment:

```bash
bash build_cxml.sh
mkdir -p build/settings-diagnostics
cp build/vita_moonlight_ui.rco build/settings-diagnostics/
cmake -S . -B build/settings-diagnostics -DCMAKE_BUILD_TYPE=Release -DMOONLIGHT_SETTINGS_DIAGNOSTICS=ON
cmake --build build/settings-diagnostics --parallel 4
```

Install **`build/settings-diagnostics/vita_moonlight_paf.vpk`**; the ordinary
`build/vita_moonlight_paf.vpk` does not enable the probes. Its matching ELF is
in the same diagnostic directory. The normal build directory keeps the
option OFF. Manifest hashes are stored under the diagnostic directory.

Connect the existing psp2shell console, for example in interactive mode:

```bash
psp2shell_cli 192.168.1.135 3333
```

Capture startup if possible, and connect before selecting Settings so the
pre-LoadSync messages are captured even if the app crashes. Do not use the
CLI's one-shot commands to collect this output: the installed CLI previously
crashed locally on a one-shot disconnect. The console requires the existing
psp2shell plugins to be active on the device; no plugin installation or
configuration was changed by the agent.

Run first without ioPlus and then with ioPlus, fully rebooting as needed and
ensuring HENkaku is active. Keep this same diagnostic VPK, other plugins and
saved data unchanged. Return the `[SettingsDiag]` lines from both runs.
If no console output is available, resolve capture first rather than trying
another authority ID or deleting settings.

### Validation

- Normal `bash build.sh` and the separate opt-in CMake build completed.
- VPK ZIP integrity, `CATEGORY=gd`, authority ID `0x2F00000000000101`, packaged
  SELF equality and debug-ELF equality were checked for both variants.
- Diagnostic marker strings are absent from the normal ELF and present in
  the enabled ELF. Existing legacy/linker warnings remain; build success is
  not Vita verification.
- `git diff --check` passed. No deployment or new Vita run was performed.
  The next required evidence is the paired diagnostic output above.

## Console capture unavailable: on-device report (2026-10-19)

The user reported no SettingsDiag lines, but did receive:

```text
[SceCommonDialog] Starting Init(), type 1
[SceCommonDialog] Term() completed, type 1
```

These establish an active console connection and some system output, not
successful capture of this application's stdout. The local diagnostic VPK
was checked again and contains the SettingsDiag marker; whether that exact
variant is installed is not yet established. The inspected local psp2shell
kernel code captures sceIoWrite only when its fd matches a stored stdout fd,
which is one possible capture limitation, not a proven explanation for this
device. No console/plugin code or configuration was changed.

### Screen-only report variant

An additional option `MOONLIGHT_SETTINGS_DIAGNOSTICS_SCREEN=ON` requires the
console diagnostic option to be ON as well. Both remain OFF by default.
When selected, LegacyMoonlightAdapter::OpenSettings calls the native settings
adapter's diagnostic report instead of initializing AppSettings. Incidental
backend settings reads return an error without loading the problem plugin
or showing UI from a worker thread.

The report uses the same SceMessageDialog family and nonblocking PAF
MainThreadCallList polling pattern already used by Main and ConnectionService.
Its text and parameter buffers have static lifetime; the poll callback ends
the dialog, and shutdown unregisters/aborts an owned report. Returning failure
from the Settings operation lets the existing Main caller restore activation.
No page or CXML implementation was modified.

The visible heading is **SETTINGS DIAGNOSTICS - SCREEN**, followed by the
embedded build revision, stat/open/read results for both native Settings
files, stat results for the ux0/uma0 moonlight directories and dependency
load results. Directory stat is only a read probe and cannot establish write
permission or the actual selected storage directory. Module reads still do
not establish loader/start success.

**This variant never attempts to load AppSettings.** Closing a successful
report therefore does not mean Settings works or the crash has been fixed.
It is for photographing/transcribing results without relying on console
capture. Actual Settings editing and streaming validation belong on a normal
build after the root cause is identified.

Rebuild separately from the repository root:

```bash
bash build_cxml.sh
mkdir -p build/settings-diagnostics-screen
cp build/vita_moonlight_ui.rco build/settings-diagnostics-screen/
cmake -S . -B build/settings-diagnostics-screen -DCMAKE_BUILD_TYPE=Release -DMOONLIGHT_SETTINGS_DIAGNOSTICS=ON -DMOONLIGHT_SETTINGS_DIAGNOSTICS_SCREEN=ON
cmake --build build/settings-diagnostics-screen --parallel 4
```

The produced VPK is in that directory. A convenience copy of the validated
package is **`build/vita_moonlight_paf-settings-screen.vpk`**, with a matching
`build/vita_moonlight_paf-settings-screen.elf`. These are ignored local build
outputs, not additional tracked artifacts. The package SHA-256 for this run
is `7e5354e2d9437ce1edfac98f87c4c6fa060ae37b1854ea92b69868dd0726e2b5`.
It embeds parent revision `54fc6c7` because it was built before committing
this diagnostic change; the manifest lives in the screen build directory.

Install this variant, keep ioPlus disabled and HENkaku active, and select
Settings. Return the report as a photo or transcription; repeat on the same
VPK with ioPlus enabled for comparison. If the heading is absent, record
whether Settings crashed, returned to Main, or another dialog appeared;
do not assume the report ran.

### Checks actually performed

- Default, console-only and screen diagnostic configurations built successfully.
  The screen build's first compile rejected the unsigned/signed message-pointer
  conversion; an explicit SceChar8 cast matching the existing dialog pattern
  corrected it, and the final screen build completed.
- All three VPKs passed ZIP integrity, CATEGORY=gd, title ID VLMP00001 and
  SELF authority ID `0x2F00000000000101` checks. Each packaged SELF and debug
  ELF matches its corresponding build output.
- Only the screen ELF contains the screen heading; default ELF has no
  SettingsDiag marker, while both diagnostic ELFs have it.
- `git diff --check` passed. No device deployment, dialog display or actual
  Settings behavior was tested by the agent.

## ioPlus-enabled probes and report validation failure (2026-10-19)

The user supplied actual SettingsDiag output with ioPlus enabled:

- AppSettings module stat and close return zero; open is `0x4003006d`, read
  returns four bytes, and the header is `53434500` (`SCE\0`).
- RCO stat and close return zero; open is `0x4002019d`, read returns four
  bytes, and the header is `52434f46` (`RCOF`).
- Dependencies `0x80000005`, `0x80000006`, `0x8000000c` each return zero.
- Screen dialog Init returns zero, followed by asynchronous
  `SceMsgDialog Parameter Error : 9` and CommonDialog termination.

Thus the diagnostic code is reached, console capture works for this run,
both files are readable and dependencies load **with ioPlus enabled**.
Four valid header bytes do not establish whole-file integrity. AppSettings
itself was deliberately not loaded in this screen variant; no opening,
initialization or persistence success may be inferred.

The dialog error is a separate defect in the diagnostic path. Its numeric
validation rule was not found in the public VitaSDK headers; Vita3K's
message-dialog implementation is not evidence of firmware 3.60's exact
validator. Do not label error 9 as a particular length/line limit without
runtime confirmation or matching firmware code.

The old report had nine explicit lines and about 291 ASCII bytes for the
reported successful read values. As an isolated diagnostic presentation
experiment, it is shortened to three explicit lines. File/module loader
behavior, authority ID and persistent data are not changed. The revision,
report byte count and directory stat results are also logged before Init;
the dialog result query, final result and button are logged before Term.
Init=0 alone is not evidence that a dialog passed asynchronous validation.

The compact report retains heading **SETTINGS DIAGNOSTICS - SCREEN**:

- `M` is `app_settings.suprx`; `R` is `app_settings_plugin.rco`. Each triple
  is `stat / open / read` (first two hexadecimal, last decimal).
- `UX` and `UMA` are directory stat results, not write tests.
- `D` gives BXCE / INI processor / Common GUI Dialog load results in hex.

The screen target was rebuilt and the VPK checked; convenience copies at
`build/vita_moonlight_paf-settings-screen.vpk` and repository-root
`vita_moonlight_paf-settings-screen.vpk` were refreshed with its matching
build ELF retained. This is still a diagnostic UI hypothesis, not a
hardware-verified dialog fix or AppSettings fix. The user subsequently supplied the ioPlus-disabled comparison below. The compact
dialog itself still needs device verification.


## ioPlus-disabled access failure and read-only FIOS follow-up (2026-10-19)

On the same screen-diagnostic variant, the user supplied:

```text
host-dir ux0:data/moonlight/ mkdir=0x80010013 selected=0
host-dir ux0:moonlight/      mkdir=0x80010013 selected=0
host-dir uma0:data/moonlight/ mkdir=0x80010011 selected=1
vs0:vsh/common/app_settings.suprx      stat=0x80010013 open=0x80010013
vs0:vsh/common/app_settings_plugin.rco stat=0x80010013 open=0x80010013
BXCE=0 INI=0 CommonGuiDialog=0
```

The logged read value `-2147418093` is the failed open result propagated by
our probe, **not a read syscall performed on a valid descriptor**. Neither
file has header bytes in this run. The report again returns Init=0 followed
by Parameter Error 9; this remains separate from the original plugin crash.

`0x80010013` is SCE_ERROR_ERRNO_ENODEV (no such device), not EACCES
(`0x8001000d`). `0x80010011` is EEXIST, accepted by the existing directory
selection code. See [Vita error codes](https://www.psdevwiki.com/vita/Error_Codes)
and the installed newlib errno definitions. Therefore:

- The uma0 fallback is directly explained by two failed ux0 attempts, without
  changing the configured paths or deleting settings.
- AppSettings files cannot be opened without ioPlus, whereas their headers
  are readable with it. The native plugin loader lacks accessible inputs in
  the failing environment. This supports the earlier absent-module finding;
  the exact PAF branch-to-zero and internal loader return remain unobserved.
- Successful sysmodule loads do not imply that arbitrary vs0 paths are accessible.
- ENODEV alone does not prove a physically absent mount or a particular authority-ID
  policy. Given [ioPlus's overlay hook](https://github.com/TeamFAPS/PSVita-RE-tools/blob/master/ioPlus/ioPlus-0.1/src/ioplus.c),
  an `invalid:` redirection is a specific testable hypothesis, not yet an observed rule.

### New diagnostic scope

Settings diagnostics now query the current process's FIOS overlay IDs (up to
128), read their info and resolve these four paths for reads: ux0/uma0
moonlight directories and the two native Settings files. Logs use
`[SettingsDiag] overlays`, `overlay`, and `resolve`. Enumeration reports the
total count and capacity, so truncation is visible; failed queries retain
raw error codes. The current thread's filtering state is read, never changed.
No add/modify/remove/ThreadSetDisabled API is imported. Production builds
have neither these imports nor the diagnostic strings.

The local SDK's `fios2kernel02.h` declares only AddForProcess02, but its 3.60
stub archive contains all four read/query exports used here. A diagnostics-only
ABI subset is declared locally from [upstream vita-headers fios2kernel02.h](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/fios2kernel02.h):
GetList02 uses bounded ID storage, a total-count pointer and an explicit byte
size; ResolveSync02 has independent maximum path and output-buffer sizes.
Both option layouts are compile-time checked at 0x18 bytes. This avoids guessing
the old header's unnamed syscall fields or changing the developer's SDK.
These calls still require hardware validation and may return a privilege error.

Install `vita_moonlight_paf-settings-screen.vpk` from the repository root over
the existing application, keep ioPlus disabled, launch and select Settings,
and capture all SettingsDiag lines. No screenshot or working report dialog is
needed: console capture already works. This build deliberately never loads
AppSettings, so it cannot validate an eventual Settings fix. If `resolve`
shows `invalid:`, match its prefix to the table and investigate the rule's
creation/policy. If it preserves the original path, investigate lower I/O
layers instead. Do not disable overlays globally or change authority IDs on
this hypothesis alone.


### Validation of this follow-up

- `bash build.sh` completed successfully with the ordinary default options,
  including CXML, ELF, SELF and VPK. The old screen subdirectory had been
  removed externally, so the new diagnostic target was configured separately
  at `/tmp/vita-settings-fios-screen` with both diagnostic options ON and built
  successfully with four jobs. Existing legacy signedness and libcurl wchar_t
  linker warnings remain; no unrelated stubs or warning suppression was added.
- Both VPKs pass ZIP integrity, packaged SELF equality, CATEGORY=gd,
  title ID VLMP00001 and SELF authority `0x2F00000000000101` checks.
  Each `.elf` equals its corresponding extensionless debug executable.
- ELF inspection confirms the FIOS marker/GetList02 symbol exists only in the
  diagnostic variant, and no ThreadSetDisabled02 symbol exists in either.
- Screen VPK SHA-256:
  `e7d95ed08b89372cd92a13cd4b763969563bb37db9c9fdcd33a80719c9e80ad8`.
  This artifact embeds parent HEAD `f11f4af` and contains the subsequent
  uncommitted diagnostic changes at build time. The local manifest at
  `build/settings-fios-manifest.json` records its hash and untested status.
  Root/build convenience VPKs and the build convenience ELF were refreshed.
- No device deployment, FIOS query result or compact-dialog display was tested
  by the agent. Runtime root cause beyond inaccessible paths remains open.


## Hardware confirms blocking FIOS overlays (2026-10-19)

The user ran the clean-master `fb4fd6a58d05` screen variant without ioPlus.
GetList02 succeeds and returns 14 rules; the thread-disabled state is zero.
Ten opaque rules at order 0x80 redirect ud0, tm0, pd0, vs0, vd0, ux0, ur0,
os0, grw0 and gro0 roots to `invalid:/`. Four order-0x81 rules map two opaque
storage names, savedata0 and app0 to their PD devices. Read resolution succeeds
and returns:

```text
ux0:data/moonlight/ -> invalid:/data/moonlight
uma0:data/moonlight/ -> uma0:/data/moonlight
vs0:vsh/common/app_settings.suprx -> invalid:/vsh/common/app_settings.suprx
vs0:vsh/common/app_settings_plugin.rco -> invalid:/vsh/common/app_settings_plugin.rco
```

The subsequent stat/open results remain ENODEV, uma0 is selected, and all
three dependencies load. The confirmed causal chain is blocking overlay ->
invalid device path -> file access failure. It explains the host-directory
fallback and makes the native plugin's files unavailable. The creator and
policy that installs the order-0x80 rules are still unknown. Do not attribute
them to an exact firmware authority check without a matching implementation
or controlled runtime experiment. Removing all rules would also disturb
app0/savedata0 mappings. ThreadSetDisabled does not skip privileged 0x80 rules
according to upstream vita-headers; it is not a proposed fix.

The compact report logs 147 bytes / three lines and Init=0. This capture ends
there; it does not prove display success, completion, or absence of a later
Parameter Error 9. AppSettings was still deliberately bypassed.

### Isolated authority-ID comparison

A new test package uses the **same compiled VELF/runtime code** as the failing
fb4fd6a screen variant, signed with `0x2200000000000001`. This value is taken
from [BHBB's VDSuiteSignElf call](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/master/CMakeLists.txt),
not asserted to be the correct final authority for our application. BHBB's
Settings working without ioPlus is user-reported; its toolchain/SFO also differ.
Only authority is varied in this experiment; normal CMake settings are unchanged.

Command applied to the existing clean snapshot's VELF:

```bash
vita-make-fself -a 0x2200000000000001 -c -at 0x0E vita_moonlight_paf.velf settings-auth220.self
```

Local binary verification confirms SELF lengths match and **only two bytes**
differ, at offsets 0x81 and 0x87 within app-info's authority ID. Repackaging
changes only `eboot.bin`; all SFO, LiveArea, RCO and other package contents
are byte-identical. The VPK passes ZIP integrity. Its SHA-256 is
`3d7fd47bca8e59334eb930e84dacccd2738f4459c410325ca3dabf31be54ae83`.
The package is repository-root `vita_moonlight_paf-settings-auth220.vpk`;
`build/settings-auth220-manifest.json` records the comparison. No recompilation
was needed for this header-only experiment. No deployment/hardware test was
performed by the agent.

Next device test: install this named variant with ioPlus disabled and collect
SettingsDiag after selecting Settings. Compare order-0x80 rules, resolved
paths and file-access results. If access changes, authority is a demonstrated
causal variable for this device; then test real AppSettings initialization,
persistence, streaming/input and PS lifecycle separately before adopting it.
If access does not change, retain the current production authority and investigate
the launcher/firmware policy. Do not migrate existing uma0 host data for the test.

At the start of this stage the primary checkout had an unfinished user-initiated
revert of f11f4af and unmerged LiveArea assets; these were left untouched. At final
verification the revert/conflicts were no longer present, master remained at
fb4fd6a and only these documentation changes remained. The documentation could
therefore be committed independently of any user changes.


## Authority comparison removes blocking rules on hardware (2026-10-19)

The user ran the auth220 screen package without ioPlus. GetList02 succeeds,
thread-disabled remains zero, and count drops from 14 to four. Only order-0x81
app0/savedata0/storage mappings remain. Both ux0 and vs0 resolve to their real
paths; ux0 mkdir returns EEXIST and is selected. Native module/resource stat,
open and four-byte read succeed with SCE/RCOF headers; dependencies all return
zero. Since the package comparison changed only two authority-ID bytes, this
establishes a causal authority-to-overlay/access relationship on this device.
The exact firmware branch creating the rules is still not identified; no
claim about every firmware's policy follows from this result.

The screen variant bypasses AppSettings. Its Init=0 log is neither an actual
Settings-open result nor proof of persistence. It also does not verify PS
Button/ShellUtil or Quick Actions under the new identity.

### Minimal build correction and remaining regression scope

CMake's final authority argument changes from `0x2F00000000000101` to
`0x2200000000000001`, matching the tested package and BHBB reference. Runtime
code, native loader, category gd, UNSAFE, SELF ATTRIBUTE 0x0E, storage paths
and formats remain unchanged. This corrects the demonstrated packaging input
that triggers the inaccessible paths; it does not remove/patch overlays at
runtime or require ioPlus. The previous comment calling the old ID known-working
was narrowed: past persistence/PS checks did not establish ioPlus-free access.

Normal diagnostics remain OFF by default. The next named test artifact enables
console SettingsDiag only, with SCREEN=OFF, so OpenSettings uses the real
moonlight_settings_init -> Plugin::LoadSync -> AppSettings Interface flow.
No diagnostic message dialog replaces it. The artifact name is
`vita_moonlight_paf-settings-auth220-live.vpk`. Confirm LoadSync returns and
found=1, Settings opens/closes, a visible change persists across a complete
restart, and ux0 host operations work. Do not migrate/delete old uma0 records
or assume previously hidden ux0 contents are current. Then test streaming,
reconnect, controller/touch, deliberate PS taps, stop and Quick Actions.

Potential regressions are authority-dependent AppSettings/SaveSafeMemory
identity/access and ShellUtil/PS behavior. Filesystem access is hardware-verified
only on the supplied 3.60 HENkaku run; actual Settings and these regressions
remain pending. No SAFE-mode or other authority survey is reopened.


### Local validation of the build correction

- `bash build.sh` completed (CXML, application, ELF, SELF, VPK) with diagnostic
  options OFF. A separate console diagnostic build with DIAGNOSTICS=ON and
  SCREEN=OFF also completed. No runtime C/C++ code was changed at this stage.
- Make reports future timestamps/clock skew on LiveArea assets. Both VPKs were
  checked against the current source assets byte-for-byte; packaged SELFs match
  generated outputs, ZIP integrity passes, SFO is gd/VLMP00001 and parsed SELF
  authority is 0x2200000000000001. No source timestamps were changed to hide it.
- Both debug ELFs match their extensionless executable. Neither contains the
  screen replacement heading; only the console variant contains FIOS diagnostic
  markers. Its compiler flags confirm SCREEN is not defined.
- The live VPK is copied to the repository root. SHA-256:
  `b2d115394e23e0974274dad125a288cef06a78ddd3f0fcf94da53aca4f964716`.
  It embeds parent revision `72819b499d4a`; build-time working changes consist of
  the authority argument/comment and documentation. The matching ELF and hash
  manifest are under build/. No generated artifacts are committed.
- `git diff --check` passed. No installation, actual Settings opening, persistence
  or PS/stream regression testing was performed by the agent.


## Real AppSettings loader returns in two device launches (2026-10-19)

The next user attachment contains two native-load runs embedding the expected
live-build parent revision `72819b499d4a`, with different process IDs. In each:
ux0 is selected; only four order-0x81 mappings exist; Settings files are readable;
dependencies return zero; `native plugin LoadSync returned found=1` is reached.
The SetParam import words change from the unresolved stub pattern
`e24fc008,e3a0f000,e1a00000` to a patched address/branch pattern ending in
`e12fff1c`. This is evidence that the real native plugin loads and the import
is resolved, beyond the earlier screen-only file probes. The original failing
stage is passed in both launches. No actual Show callback or settings-value
readback is recorded, so visible open/close behavior and persistence still
require user confirmation. The earlier stated no-ioPlus test condition is
being confirmed together with that outcome.

Both runs also include `RefreshHosts hosts=1 cells before=1 after_delete=0
after_insert=1 selection=0`. This verifies that single-host refresh in those
runs, not the full empty/one/multiple-host cold-start matrix. No Main refresh
change is justified.

The attachment starts with a stream-stop sequence: PS double press, stop worker,
audio drain/release success, Limelight cleanup, manual STREAM_STOPPED, worker
finished and three successful video unmaps/deferred release. The excerpt has
no startup/build marker tying that stream to the new authority variant; do not
mark its PS/stream regression complete until provenance is confirmed. IDR waits
and dropped audio occur before the stop, so the capture also does not establish
loss-free streaming. ENet/control errors during teardown alone do not establish
a separate persistent failure after cleanup. No runtime change follows from
this attachment.


### User confirmation and closure of the reported Settings regression

The user explicitly confirms both follow-up questions: on the new authority
VPK **without ioPlus**, native Settings opens/closes and changed values survive
complete application restart; the stream-stop excerpt also belongs to that
same new variant without ioPlus. The originally reported Settings crash and
persistence concern are therefore resolved on the tested 3.60 HENkaku device.
The recorded double-PS stop, audio cleanup and video deferred release count
as regression coverage for this variant. The earlier provenance/persistence
questions above are answered by this confirmation, not by guessing from logs.

Do not extend that result to other firmware, Quick Actions/hold/short-tap
behavior, reconnect, network-loss-free streaming or new saved-host writes.
Those remain separate checks. Default builds keep diagnostics OFF; no further
runtime patch or Main refresh change is needed for this Settings issue.
This stage changes documentation only: source/log review and diff checks were
performed; no rebuild/deployment was necessary or performed by the agent.


### Remaining PS/Quick Actions/reconnect checks confirmed (2026-10-19)

Responding specifically to the outstanding Quick Actions, short PS press and
reconnect checks, the user confirms that all also work. In the ongoing context
this is the new authority VPK on the same 3.60 HENkaku device without ioPlus.
Together with the previous explicit Settings/persistence/double-PS stop
confirmation, the requested authority-change regressions are covered on this
device. No additional logs or timing measurements accompany this confirmation;
do not infer network-loss-free behavior, new host-write persistence, every
input setting combination or compatibility with other firmware. No runtime
change or rebuild is needed. The next independent implementation item remains
API audit F1 (unexpected stream termination cleanup), subject to current source
review, rather than further speculative Settings/PS changes.


## Diagnostic cleanup after hardware verification (2026-10-19)

Removed the SettingsDiag file/header/import/FIOS probes, local syscall ABI
subset, screen report and its polling/shutdown logic, temporary init bypass,
backend/header entry point, CMake options and diagnostic FIOS linkage. Restored
direct dependency loads and the ordinary debug-toggle gate; no special prefix
can force console output. Host mkdir no longer carries a diagnostic out-parameter.

Removed the raw ShellUtil callback, synthetic-edge, press-pending and release
duration traces. Kept the existing PS/overlay state machine, compatibility
heuristic and useful state-transition/error/start-stop logs. Main host-refresh
and stream lifecycle diagnostics required by project guardrails remain intact.
The authority correction 0x2200000000000001, native Settings, host storage,
streaming cleanup and debug ELF output remain in place.

Obsolete named Settings diagnostic VPK/ELF/manifest artifacts and temporary
investigation output are removed locally after verification. Original user
attachments/dump files are not touched. This record retains the evidence and
hardware verification limits; no new device test is implied by cleanup.


Cleanup validation: `bash build.sh` completed through CXML/ELF/SELF/VPK. ELF
inspection confirms SettingsDiag, screen-report and removed raw PS trace strings
and FIOS probe symbols are absent; the required Main RefreshHosts diagnostic
remains. Debug ELF equality, ZIP integrity, packaged SELF equality and authority
0x2200000000000001 checks pass. Removed stale diagnostic CMake cache keys and
28 obsolete generated artifacts/temporary outputs plus upstream research scratch
files. `git diff --check` passes. No Vita test of this cleanup was performed.
