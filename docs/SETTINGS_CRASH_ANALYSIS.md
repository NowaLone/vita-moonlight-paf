# Settings crash: full-dump analysis (2026-10-19)

## Scope and evidence

The user reports a freeze followed by C2-12828-1 after selecting Settings on
Main. Saved PCs appear on startup in the reported run. This document records
offline findings; it does not establish a fix or hardware verification.

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
