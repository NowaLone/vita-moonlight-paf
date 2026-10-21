# LiveArea text probe

Opt-in package. It does not change streaming, host storage, or the LiveArea of a normal build.
The committed `template.xml` stays the installed default. This process rewrites labels only after
launch, and only when the probe option is compiled in.

The probe answers four device questions:

1. Can the app write a text-only LiveArea template?
2. What does `scePromoterUtilityUpdateLiveArea` return for `VLMP00001`?
3. After a full quit, does reopening the bubble show `Test PC N` and `Test App N`?
4. Does tapping frame 1 deliver `psla:eboot?probe=1&rev=N` to `sceAppUtilAppEventParseLiveArea`?

`N` increases by one on every launch of this package. Frame 7 is the PC label. Frame 1 is the
app label and the only parameterized target. Frames 2–6 keep their images and have no target.
No box art is generated.

## Build

The agent must not run this command. From the repository root:

```bash
./build.sh -DMOONLIGHT_SYSTEM_APP=OFF -DMOONLIGHT_LIVEAREA_PROBE=ON
```

Install `build/vita_moonlight_paf.vpk`. Do not combine the probe with a disconnect test.
`./build.sh` resets `MOONLIGHT_LIVEAREA_PROBE` to OFF unless the argument is passed again.
Reinstalling a normal package restores the original LiveArea files.

## Device steps

1. Launch the bubble from the gate, not from a frame. Quit with the PS button until the app
   is fully closed, not only suspended.
2. Open the bubble again without starting the app. Check frame 7 for `Test PC N` and frame 1
   for `Test App N`. Note whether the card is blank or still shows the old labels.
3. Tap frame 1 (`Test App N`). The app starts and records the launch string.
4. Quit fully again. Copy `ux0:data/moonlight/livearea-probe.log`. psp2shell also prints the
   same `[LiveAreaProbe]` lines. Debug log in Settings is not required.

If the promoter call reports failure, the same launch already tries two direct writes:

- `ux0:app/VLMP00001/sce_sys/livearea/contents/template.xml`
- `ux0:appmeta/VLMP00001/livearea/contents/template.xml`

If the promoter call reports success but the bubble does not change, create an empty file
`ux0:data/moonlight/livearea-probe.force-fallback` and launch once more. That forces the direct
writes. Delete the file afterward so a later success is not mixed with them.

A database rebuild or another VPK install discards the rewritten card. Launch the probe package
again to publish the next revision.

## How to read the log

`receive` / `parse` are from the launch that just happened, before this run publishes the next
revision. The first gate launch should log `no launch event` or an empty target. The launch
after tapping frame 1 should log the target string exactly as the shell delivered it.

`promoter_ok=1` means init, update, state 0, and result 0. It does not by itself prove the
shell displayed the text. The visible `Test PC N` label is the display check.

`assets copied` must be `9/9` before trusting a promoter failure: the payload directory is
`ux0:data/moonlight/la` and contains `sce_sys/livearea/contents`.

The promoter module is not resident. The probe loads `SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL`
for the call and unloads it before the UI starts. A gate launch has event type 0; that is not a
LiveArea target, and it is not parsed. A type 0 parse returns `0x80100600` and prints
`SceAppUtil Parameter Error`.
