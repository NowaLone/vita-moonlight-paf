# Settings header and Wi-Fi indicators

Read-only resource/ELF investigation, 2026-10-23. This supplements the screenshot
measurements in [UI.md](UI.md); it does not change Moonlight's interface. **[code]**
below means decoded RCO or inspected ARM instructions, not native execution.

## Supplied files and method

The maintainer supplied these firmware files; their exact firmware version was not
established. Do not generalize the hashes/layout to every firmware:

- `system_settings_main.rco`, SHA-256
  `b5dcc4cd3f7c3c0705989dacc4bf5668c7038418a918a483f80de038bc9b02bf`.
- `system_settings_plugin.rco`, SHA-256
  `1a460c4ea1b58d38d3289b96211a6c88ec7da56495a84f6a1b3dddff4bdddd4e`.
- Settings `eboot.bin.elf`, SHA-256
  `e1a80dfa535e6d5b6eed80ef3fb559bd099cec3fbc1152e93a2adcf49913f788`.
- `wifi_settings_plugin.rco`, SHA-256
  `564c0ba09f9ecdf7fd7bde1005ff481997f9c8cebfa1709e0a8f3e016dea0f37`.
- Previously supplied `common_resource.rco`, SHA-256
  `6ee58e58d1e78fecd99ea93e585c81efde3e940e3e0ca5cec4e64ba7cacdc806`.
- Screenshot `2026-10-23-022829.jpg`, 960 x 544.

RCO tree/attribute/file tables were decoded without compiling resources. Compressed
payloads were decompressed with zlib; GIM palette pixels and alpha were exported to
PNG. Main/plugin/Wi-Fi contain respectively 221/40/265 nodes and 17/17/18 textures.
Unknown names below are written as `hash_XXXXXXXX`; those are descriptive labels
for original ID hashes, not recovered Sony resource names. RCO node offsets are
relative to the tree, whose file offset is `0x50` in these files.

The ELF identifies itself as `SceSettings`. Its first LOAD maps file `0xe0` to
`0x81000000` (size `0x2938dc`); its second LOAD maps file `0x293a00` to
`0x81294000`. Code addresses below are virtual addresses in this supplied image.
Import NIDs were matched against the installed VitaSDK/PAF databases; unresolved
imports retain numeric NIDs. No builds, module execution or hardware tests.

## Header: the exact resource is local to Settings

**[code]** `system_settings_main.rco` has this complete header composition:

```text
template hash_b047ffd3                         tree 0x23c
  plane hash_bebf0207                          tree 0x278
    style _common_style_plane_transparent
    align 0,2; size 960,64; pos 0,240
    box hash_d2290b7b                          tree 0x310
      horizontal layout; adjust 2,2; centred
      text main_page_title_text               tree 0x3d8
        style style_text_header
        size 760,0; align 0,4; adjust 0,2
    plane hash_966f95fd                        tree 0x480
      style hash_8d082ccd
      size 960,2; pos 0,0; anchor 0,1
  button hash_8211f03f                         sibling of header plane
    size 46,46; pos 32,208; anchor 1,0
```

`style_text_header` (tree `0x24bc`) explicitly binds its inner `textobj`:
32 px, bold, opaque white, centred X/Y, ellipsis enabled. The box can adjust to
content; the plain header text slot is 760 px wide. This is an ordinary PAF
plane/box/text composition supplied to the Settings UI, not evidence of a separate
PAF widget class called a system title bar. The sibling back-style button exists
in the template; its visibility on a particular page depends on runtime code.

The separator chain is **[code]**:

```text
plane hash_966f95fd
  -> style_plane hash_8d082ccd                 tree 0x2444
  -> planeobj.texture0 = hash_69cb3790
  -> texture hash_69cb3790                     tree 0x38c4
  -> compressed payload file 0x5530, length 0x322
```

The payload is a 960 x 2 indexed GIM, 3152 bytes after decompression. Its SHA-256 is
`de801d8a31d34014730390cf6250a39a05b9e1ba2a52e7dbf14222a52009f7e1`.
There is no `div_mode` or margin metadata on this texture. Row 0 is black, maximum
alpha 68/255 (26.7%); row 1 is white, maximum alpha 52/255 (20.4%). It fades toward
the sides; nonzero-alpha bounds are `(36,0)-(922,2)`.

This is **not** `_common_texture_ruled_line`. The supplied common texture is
328 x 4, with transparent outer rows, `div_mode="3x1"`, `div_ratio_h≈0.49`, and
maximum black/white alpha 90/18. The former project `header_line.png` was also
960 x 2, but had alpha maxima 51/46; decoded pixels are not identical to Settings.
The 150 x 4 Wi-Fi-local line (`hash_0cec900a`, margin 1/1, 912 x 2 template) is
another separator and must not be confused with this header asset.

**[code]** The ELF loads `vs0:app/NPXS10015/system_settings_main.rco` through
`paf::Plugin::LoadAsync` at `0x810001fa` (path assembled at `0x81000190`). Wi-Fi
page handling accesses the shared `main_page_title_text` at `0x8112081c` and calls
`Widget::FindChild` at `0x81120862`; this path replaces the title with the saved
AP name for `page_wifi_saved_ap_menu`. No direct reference to the header template's
numeric hash was identified in the ELF; the exact shared framework constructor
path was not fully reconstructed. The RCO dependency chain itself is explicit.

**[shot]** The screenshot has the system infobar above y≈32, a header band through
y≈96, and dark/light separator rows around y=95/96. Bright title pixels span
approximately x=370..588, y=49..82, including the descender and JPEG edges.

## Wi-Fi dot: connection state, with an animated intermediate state

**[code]** The Wi-Fi plugin owns two 30 x 30 textures used by this code path:

- `hash_fbd879ae` (texture node `0x5144`): bright green sphere/glow.
- `hash_9718c305` (texture node `0x51b0`): dim olive sphere with a bright upper half.

`hash_a6c087bd` is a translucent ring texture in the same file. Its presence does
not establish a third AP status: the traced Wi-Fi paths below switch the two sphere
textures. An ELF use of the ring at `0x81019dca` is in Bluetooth-plugin handling,
so a shared hash alone must not be assigned Wi-Fi semantics without plugin context.

The Wi-Fi template `hash_e454df4f` (tree `0x1808`) creates a `button`
`hash_c3232534` (tree `0x1834`), size 27 x 27, pos -302,0. Its style
`hash_1401d780` (tree `0x2a38`) binds the green texture as `glow_obj`.
The executable also creates a plain 27 x 27 PAF `Plane` dynamically. Runtime names
are `icon_blink_button` and `icon_blink_plane`; these names are ELF strings,
not invented names for the RCO IDs.

In the row-rendering routine beginning at `0x81124dfc`, the AP entry's word at
offset `0xb8` selects the presentation (`0x81125944`):

- States 2, 3 or 5 enter the animated branch (`0x81125948..0x81125952`). It uses
  the dim base texture (`GetTexture` at `0x81125d80`), opens the button template
  (`0x81125dfe`), obtains the green texture (`0x8112601a`) and calls
  `Widget::SetLoopEffect(7, true)` (`0x81126052`).
- State 4 enters the steady branch (`0x81126060`), uses the green texture
  (`GetTexture` at `0x81126480`) for the plain indicator, and removes the separate
  blink-button widget when present.
- Other states use the dim texture (`GetTexture` at `0x8112675e`) and remove the
  blink-button path when present.

A second indicator-rendering path contains the same state-4 test at `0x8112a3d6`,
green texture lookup at `0x8112a5d0`, dim lookup at `0x8112a650`, and animated
`SetLoopEffect(7,true)` at `0x8112a3d0`. Both paths pass the retrieved surface to
virtual slot `0x100`, matching `Widget::SetTexture(surface)` in the installed PAF
header. The exact timing/visual waveform of loop effect 7 was not measured; do not
invent a blink frequency from its numeric value.

The state is not taken from row focus. The status updater obtains it through
`0x8112d1fe -> 0x811401ac -> SceNetCtl NID 0xe8e8df18`, then writes that value to
the matching AP entry at offset `0xb8` (`0x81137d7a`). The connecting branch
(`0x81137bd8..0x81137be8`) tests 2/3/5 and selects message hash `0x500e7607`;
the connected branch selects `0xda7e42d0` (`0x81137cee..0x81137cfa`). Decoding
the English locale embedded in the Wi-Fi RCO gives respectively:

- `0x500e7607`: `Connecting... (%1)`.
- `0xda7e42d0`: `Connected. (%1)`.

The preceding branch bounds relevant active AP states to 2/3/4/5
(`0x811378f6..0x81137906`), so the connected alternative is state 4. This confirms
the interpretation: animated green during connection, steady green when connected,
dim otherwise. It does not mean that dim APs are unreachable. Individual names of
the intermediate states and the private NetCtl import remain unresolved.

**[shot]** Dot centres are approximately x=178, y=327/397/467/537, with 70 px row
pitch and title origin x≈208. This matches the 27 px slot and -302 position in a
960 px parent. The screenshot's OpenWrt is green and the summary says it is connected.

## Consequences for Moonlight

At the research baseline, Main/Apps had a comparable PAF plane/text/separator
composition, but used 26 px regular left-aligned text at x=80, without the centred
adjusting box. Search had an independent separator and a conditional empty-list
label at x=64, rather than a persistent title. Pairing and Stream lacked this
standard header. Resource reuse alone did not make those headers identical to Settings.

The previous replacement of the project's PNG with `_common_texture_ruled_line`
reuses a native PAF resource, but does **not** reproduce the exact Settings header
texture. The investigation itself did not change appearance.

**Requested implementation follow-up, 2026-10-23 [code]:** Main/Search/Apps now
share the Settings-style plane/horizontal-box/centred-text composition, with a
32 px bold title and a 760 px text slot. The maintainer explicitly chose
`_common_texture_ruled_line` for the separator; its plane retains 960 x 4 geometry.
Search's permanent localized title is separate from its conditional empty-list
label. Pairing/Stream retain their existing layouts and corner-button navigation
is unchanged. See UI.md section 5.1 for current details and pending Vita validation.

The sphere artwork can be reused for a PC availability indicator, but availability
is a separate product meaning. Saved-host `fill_host()` currently sets `online=0`;
mDNS discovery sets `online=1` on arrival, without expiring results during that
scan. Wiring those fields directly would make all saved PCs dim regardless of
actual reachability. A real availability feature needs backend/service-owned
observations and main-thread UI delivery, preserving the existing row/checkbox
interaction. Do not derive availability from pairing or run network calls in the
row factory. Artwork extraction, state-branch inspection and XML checks do not
validate such a feature; no PC indicator has been implemented by this research.

## Applying the indicator to PC rows: implementation assessment

**Source investigation, 2026-10-23.** The proposal below was followed by the
[trial implementation](#trial-implementation); hardware validation remains open. The useful meaning
for the PC browser is recent reachability of the configured GameStream endpoint.
It should not inherit Wi-Fi's connected/connecting meaning without an explicit
product decision. Neither a stored `paired` flag nor `ConnectionService::State()`
provides live availability for every row.

### Existing mechanisms and their limits

- `legacy_device_store.cpp::fill_host()` assigns `online=0` without a request.
  `legacy_host_discovery.c::on_mdns_host()` assigns `online=1` on discovery; duplicate
  entries return early. Its worker lasts 100 x 100 ms, not continuously, and the
  published host array does not expire entries. The sniffer's `last_seen` is used
  for table-slot replacement, not a per-PC reachability lifetime. Missing mDNS
  does not establish that a saved/manual/remote PC is offline.
- `ConnectionService::Connect()` reserves foreground ownership, opens native busy,
  cancels abandoned pairing and queues `ConnectJob`. `moonlight_api_connect_host()`
  replaces the current server, invalidates Apps and may persist host identity.
  Main/Search open Apps or Pairing after success. Reusing Connect as a probe would
  change application state; showing its row pulse would usually duplicate the
  existing busy dialog before the list is covered by the next page.
- `legacy_gamestream_init()` is not a stateless probe even with a temporary server
  object: it loads/creates global client identity/credentials and initializes curl.
  `legacy_gamestream_shutdown()` frees global `s_cert`/`s_private_key`, clears
  `s_unique_id`, and calls `curl_global_cleanup()` (lines 2079..2106 at this review).
  Its HTTP path also shares a mutex and global last-error state. A probe must not
  borrow that init/shutdown pair or the active server's curl handle.
- Existing `load_server_info()` already implements `/serverinfo` parsing and pin
  verification through its transport, but is static, mutates a server snapshot,
  and requests a 15-second timeout. `http_request()` sets a 10-second connect
  timeout. These are foreground connection settings, not proposed probe settings.
  `legacy_gamestream_refresh_server_info()` requires the current paired server.
- The installed VitaSDK already exposes socket connect, resolver and epoll APIs;
  no new networking dependency is necessary for a bounded TCP-port check. Their
  use for this feature is not compiled or hardware-tested by this investigation.

### Recommended minimum

Start with two artwork states and a short, cancellable TCP connection attempt to
the same `host.internal`/`host.port` route used by the current Connect path. Resolve
hostnames outside the PAF thread. A successful handshake means **the configured
port answers**, not authenticated Sunshine, successful pairing, an idle host app,
or guaranteed streaming. A different service on that port can give a false positive.
If the product requires confirmed Sunshine identity rather than port reachability,
this minimum is insufficient: use the `/serverinfo` option below from the start.

Green means a recent positive observation. Dim means no fresh confirmation,
including unchecked, expired, failed and locally cancelled checks; do not claim
that every dim PC is powered off. Keep the last protocol/transport outcome distinct
internally from a local execution failure. Expire positive observations, keep them
only in memory and never persist availability in `device.ini` or AppSettings.

One backend-owned scan, exposed through existing HostService/backend boundaries,
can publish a complete observation snapshot. Check on entry/return to the PC list;
stop scheduling and cancel/reap outstanding work before admitting a conflicting
foreground action, leaving the page, opening Settings or entering streaming.
Deliver cleanup completion asynchronously; do not block PAF by joining a network
worker on its main thread. Bound DNS/connect time and the scan's resource use. A future periodic refresh must
run only while the list is active and must not enqueue overlapping scans. This
initial entry/return refresh is a snapshot feature, not continuous monitoring.

Results need stable identity and a scan generation: list indices, row tokens and
discovery IDs are not persistent PC identity. Reject results for deleted/replaced
hosts or obsolete scans. Use UUID/address+port matching consistent with the existing
host-identity rules, without changing persistence formats or reassigning identities.
Queue notifications through MoonlightApp; worker callbacks never touch PAF.
`HOSTS_CHANGED` is coalesced as a snapshot invalidation, so the backend must retain
all observations: its event `host_id` cannot carry the only copy of a per-PC delta.

### UI integration

Reuse the extracted green/dim PNG artwork (originally 30 x 30) in a 27 x 27
noninteractive `plane` in both `template_host_list_item` and its selection variant.
Keep the PC icon and checkbox. A proposed layout puts the normal dot at row
x=178 and the title/address at x=208; selection adds the existing 80 px offset
(dot x=258, text x=288). Text widths 667/587 retain the current right edge x=875.
These are source-layout proposals, not measured Moonlight screenshots or Vita checks.

Factories initialize the dot from the latest observation; main-thread updates
change only existing generated cells (`ListView::GetListItem` is declared by the
installed PAF SDK). Missing/not-yet-generated cells simply use the snapshot later.
Do not keep stale widget pointers or rebuild the full list for every result:
current `RefreshHosts()` deletes/reinserts cells and regenerates row tokens.
Keep keyboard focus, scrolling and multi-selection stable. Source checks and a
Vita run must confirm generated-cell lookup and texture switching behavior.

Omit the connecting pulse in the first version: it adds little behind the existing
modal busy dialog. If explicitly requested later, use native PAF loop effect 7 for
the selected connection attempt and remove it on every terminal/cancellation path;
do not invent a second animation timer or replace native foreground progress.

### When `/serverinfo` is required

A read-only probe should reuse existing parsing, URL/port and pinned-certificate
rules while keeping per-request handles/results isolated from the active session.
It must not initialize/persist a PC, generate credentials, pair, select an app or
clear global curl/crypto state. Global client/transport ownership must first be
made safe for this use; copying today's init/shutdown path is not that separation.
Never turn a TLS-pin failure into success with a plaintext fallback. Unpaired
discovery can establish service presence without establishing trust, and pairing
remains a separate action. HTTP reachability does not mean that `currentgame` is
zero; a busy Sunshine host can still answer and the existing remote-Quit flow
owns launch conflicts. Keep network/state changes separate from the artwork change.


### Trial implementation

Implemented on 2026-10-23: the minimum TCP reachability variant, two static Wi-Fi
spheres in Main/Search and selection rows, no connecting pulse. Current geometry,
refresh/expiry policy and ownership are canonical in [UI.md §5.2](UI.md).
HostService owns a dedicated one-worker PAF queue, and the adapter exposes a
stateless native socket probe. Immutable per-job host snapshots isolate obsolete
results. UUID plus exact address/port binds observations to rows; names and row
indices do not carry identity. Cancelling a scan invalidates all of its positives.
The active job is checked on the main thread; observations and expiration become
a coalesced MoonlightApp event, not worker-to-widget calls.

The trial requests native resolver timeout/retry 1/1 and checks cancellation when
that call returns. TCP waits have a one-second process-clock deadline, checking
cancellation every 10 ms. DNS timing and resolver cancellation latency are native
acceptance items, not a claim established by Python mocks. No libcurl or GameStream
session state is involved. Enqueue failure remains dim and preserves the previous
successful job as Connect's cleanup barrier. Teardown waits for the queue before
network termination. Background network activity runs only for a requested active
list scan. Follow-up after the maintainer observed Main's dot dimming over time:
active Main/Search now repeat unchanged scans at a 15-second start-to-start
interval, waiting for the previous scan to finish. Pending replacements preserve
fresh previous positives with their original 30-second expiry, avoiding a dark
flash on every refresh. A failed check still turns dim immediately. Hidden lists
and conflicting workflows cancel scheduling as before; long serial scans may
still exhaust freshness. Periodic refresh needs maintainer build/Vita validation.

Non-build checks interpret the actual probe/predicates with mocked Vita results:
TCP immediate/pending success, refused/wrong/error readiness, SO_ERROR failures,
creation/setup/poll failures and acquired-handle cleanup, timeout/cancellation,
DNS success/failure/cancellation, identity replacement and the exact TTL boundary.
Submission failure preserves the drain barrier; source wiring checks cancellation
at foreground admission and waiting inside Connect's worker. Event checks cover
coalescing and main-thread dispatch. These do not run native threads/networking.

Maintainer acceptance: build CXML/application; compare Main/Search/selection dots
with the official screenshot; scroll enough rows to force cell reuse; confirm
selection/focus survive result changes; test answering/refused/silent ports,
hostnames and DNS failure, active-list refresh across several minutes, disappearance
when a PC stops answering, 30-second expiry and list return refresh; select a PC
while a check is pending; leave/reopen Search, IME and Settings; stream/stop/return;
exit during a pending check. A positive from an unrelated TCP service is an
intentional limitation of this trial, not proof of authenticated Sunshine.
