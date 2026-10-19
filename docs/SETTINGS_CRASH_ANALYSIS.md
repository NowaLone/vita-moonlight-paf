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

### Hardware test still required

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
