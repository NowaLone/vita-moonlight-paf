# Sunshine / GameStream client API audit

Audit date: **2026-10-18**. Local baseline: `master`,
`761d730f53a30dc8201286f2d10e22a58a1c609a`; clean working tree before this documentation change.

This continues [the preliminary audit](../ROADMAP.md#gamestream-client-api-coverage-and-follow-up-2026-10-15)
and the findings in [REPOSITORY_ANALYSIS.md](../REPOSITORY_ANALYSIS.md). It is a **source-level audit**,
not a new Vita/Sunshine interoperability test. The original inventory changed no runtime code or
storage; dated source follow-ups below describe later implementation changes (latest: 2026-10-22). `PROGRESS.md` was absent. The supplied PRD was read as technical context;
current source and Git state determine the findings below.

**Current decision, 2026-10-21 (local edf0b58):** the required normal client flow has effective
implementations and scoped Vita evidence. Further routine stop/reconnect test variants are not
the next development task. See [the consolidated decision and backlog](#6-completion-and-next-work)
for implemented paths, concrete code gaps, blocked self-unpair and deferred verification.
Later source follow-ups isolate snapshot bursts from ordered lifecycle delivery (F5),
propagate host metadata write failures (L8), remove Main/Search's eight-PC presentation limit
and simplify discovery lifecycle/start errors (A01); broader Vita validation remains pending.
The scoped 6af2e36 Search/connect/Apps runtime result below does not close all remaining cases.
These corrections add no protocol endpoint.

**Startup regression takes priority, 2026-10-22:** the maintainer's new core shows a Main null
write consistent with the installed PAF sized-vector constructor reading uninitialized capacity.
That constructor was introduced for row state in `02541bc`. Main now constructs empty then
resizes; the test normalizer that hid this SDK behavior is removed. All 127 non-build checks pass.
See [dump/source evidence](MAIN_STARTUP_CRASH.md). A later maintainer system-profile log identifies
`6af2e3639c69` and reaches one-PC Main refresh, two discovery cycles, paired Connect and four
Apps cells without the previous crash. Initial cold-start timing, natural timeout vs explicit
Stop, >8 hosts and stream/error paths are not separately verified by it.
Do not treat earlier passing host-list source checks as device/allocator validation.

**Ordinary stream follow-up, 2026-10-22:** the next fragment after the 6af2e36 Main/Search run
shows same-app encrypted-RTSP resume/setup=0, StreamPage, 411 decoded frames and audio packets,
then deliberate double PS → owner stop=0/audio drain/release=0/three unmaps=0/deferred release.
ENet interrupted/control-failed:4 occurs during requested teardown and matches local common-c's
ConnectionInterrupted/EINTR path. First video/audio packets arrive at 500/5400 ms; the maintainer
explicitly confirms image and audible sound for this attempt. This fragment has no independent launch marker
or exact Sunshine release. USER_RW free changes 33836→33068 KiB; newlib used 95704→99000 bytes.
One cycle neither diagnoses a leak nor establishes full memory return. No runtime change;
[full scoped record](MAIN_STARTUP_CRASH.md#subsequent-ordinary-stream-startstop-log--2026-10-22).

## User runtime follow-up — 2026-10-20

After the changes through source HEAD `22ff3ae`, the maintainer replied “всё ок” to the
requested Vita sequence: repeat PIN pairing → Apps → application restart → connect without
another PIN → launch and stop streaming. This is user-reported success of that normal flow,
covering parts of T3/T4/T5/T8/T9 and indicating reuse of saved trust after restart. It does
not prove certificate-mismatch rejection, wrong-PIN handling, malformed XML, different-app
conflict, same-app resume, two-host isolation, network-loss cleanup or failed pin writes.
The installed package revision, build log, Sunshine version/configuration, certificate-file
inspection and TLS trace were not separately supplied. No build or hardware run was performed
by the agent. Earlier source-only evidence statements below retain their original scope; this
follow-up adds only the explicitly requested normal-flow smoke result.

### Different-app conflict — user and log verified, 2026-10-20

The maintainer confirmed the expected behavior: selecting B while A runs produces an error,
A keeps running and Vita does not attach to A in place of B. Supplied psp2shell evidence:

```text
[ConnectionService] Start app=881448767
[GameStream] server uniqueid=3D7D0EDF-A6A6-F303-0A1C-217868AB07D4
[GameStream] app conflict selected=881448767 active=1093255277
[Stream] setup finished app=881448767 result=-8
```

The fresh serverinfo log precedes conflict detection. Current L2 source returns before the
launch/resume request and RTSP setup in that branch. The earlier `Start ... result=0` means
the asynchronous worker was scheduled; it does not mean streaming succeeded. The terminal
result -8 is the intended conflict failure. No automatic remote Quit is performed.
This verifies the different-app portion of T5/F2 on the tested Vita; same-app resume and
host-app exit/change still need separate tests. Logs show mode=2, total=65536 KiB and
newlib=6144 KiB; installed package revision and Sunshine version were not supplied.
The two arena samples alone do not establish leak-free repeated failure recovery.

### Same-app resume — user and log verified, 2026-10-20

The maintainer confirmed the expected successful connection to the already running app and
provided the complete setup log. The supplied evidence includes (LAN address redacted here):

```text
[GameStream] resume app=1093255277 host=<host>
[GameStream] launch/resume accepted session=rtspenc://<host>:48010
[Stream] stage complete 4 (RTSP handshake)
[Stream] connection started
[Stream] setup finished app=1093255277 result=0
[Video] decoded frame=1 number=4
[Video] decoded frame=901 number=904
```

All setup stages 1–11 completed, including audio and input establishment. Audio initialized
at 48000 Hz/stereo; input started with motion=0. The requested stream configuration was
960×544 at 60 fps, 10000 kbps, SOPS=1; logs identify server protocol appversion 7.1.431.-1
and codec mask 0x001c0301. This is not a measured sustained frame-rate/bitrate result or an
independent audible-audio/controller/motion verification. The log ends during playback and
does not show this session stopping or subsequent resource recovery. It does confirm that
resume passed HTTP acceptance, RTSP setup, connection startup and actual video decoding.
The earlier different-app rejection and this same-app resume cover those two T5/F2 branches.
Host-app exit/change after entering Apps still needs its own fresh-state test. Exact package
revision, Sunshine release/configuration and firmware remain unspecified. No code change or
agent-run build/hardware test was needed to record this result.

### Host-app exit while Apps remains open — user and log verified, 2026-10-20

The maintainer confirmed stopping the local stream, closing the application on the PC,
then selecting it again in Apps without reconnecting the host. The new log shows a fresh
serverinfo response followed by launch rather than stale resume (LAN address redacted):

```text
[GameStream] launch app=1093255277 host=<host>
[GameStream] launch/resume accepted session=rtspenc://<host>:48010
[Stream] stage complete 4 (RTSP handshake)
[Stream] connection started
[Stream] setup finished app=1093255277 result=0
[Video] decoded frame=1 number=3
[Video] decoded frame=841 number=963
```

Stages 1–11 completed at the requested 960×544/60 fps, 10000 kbps, SOPS=1 configuration
(mode=2, 65536 KiB total, 6144 KiB newlib; protocol appversion 7.1.431.-1). Together with
different-app rejection and same-app resume, this verifies the basic F2 decision regression
on the tested Vita. Remaining T5/T8 cases include the host changing between the snapshot
and request, other host-change scenarios and network-loss teardown/reconnect. The exact
installed revision, Sunshine release/configuration and firmware remain unspecified.

Streaming quality is a separate observation: after the first decoded frame, common-c logs
`Leaving speculative RFI mode after OOS video data at frame 55`, an IDR wait, consecutive
drop limit and an IDR request. Decoding then resumes at decoded frame 61 / host frame 183
and continues through decoded frame 841 / host frame 963. This is recovery evidence, not
an error-free playback or sustained frame-rate claim. The log does not establish whether
the trigger was network loss, server output, decoder behavior or another cause. No code
change is justified by these lines alone. This attachment ends during playback and does
not contain teardown or unexpected-disconnect recovery.

### Unexpected control disconnect → cleanup → manual resume — log verified, 2026-10-20

In response to the requested network-loss test, the maintainer supplied a log containing an
established stream, an unexpected ENet control disconnect, complete local teardown, deferred
framebuffer release and successful manual stream restart. Key events in supplied order:

```text
[Limelight] Control stream received unexpected disconnect event
[Stream] connection terminated result=-1; cleanup requested
[Stream] stop worker start (session owner)
[Audio] drain port=6 result=0x00000000
[Audio] release port=6 result=0x00000000
[Stream] stop worker finished result=-1
[Video] frame[0] release unmap result=0x00000000
[Video] frame[1] release unmap result=0x00000000
[Video] frame[2] release unmap result=0x00000000
[Video] deferred frame buffers released
[LegacyAdapter] StartApplication app=1093255277 has_host=1 state=2
[Stream] connection started
[Stream] setup finished app=1093255277 result=0
[Video] decoded frame=361 number=484
```

Between the quoted stop-worker events, common-c stops input/audio/video/control and cleans
input/video/control/audio/platform. Current L2 source invokes LiStopConnection in that owner
path before its terminal event. The -1 stop result preserves the triggering connection error;
it is not evidence that teardown itself failed. Deferred framebuffer release finishes before
the next start. The new start refreshes serverinfo, uses resume for app 1093255277, receives
an accepted rtspenc URL, completes stages 1–11 and decodes video. This verifies the basic
established-stream F1 regression on this Vita. The exact network manipulation, installed
revision, firmware and Sunshine release/configuration are not independently documented.

The second StartApplication has has_host=1/state=2 and the API reports initialized=1. No
DisconnectHost, CONNECTION_CLOSED or new ConnectHost is shown: this is stream cleanup and
manual resume with the existing paired host, not proof of the separate host-disconnect API
or automatic reconnection. Pending-launch cancellation, setup/callback races, full host
shutdown, repeated failure cycles and the remaining T6–T8 matrix are still open. Free user
memory rises from 27428 KiB after stop to 33612 KiB after deferred framebuffer release;
the next setup completes with 10844 KiB free. These samples support buffer-release ordering,
not a general leak-free or sustained-performance claim.

**Audio observation and subsequent user confirmation, 2026-10-20:** teardown logs
`No audio traffic was ever received from the host!`. In local common-c AudioStream.c this is emitted when
receivedDataFromPeer is still false; the flag changes on the first receive large enough to
contain an RTP header. Audio renderer initialization/drain/release do not prove audio packets
were received or heard.
The maintainer subsequently replied “звук есть”, confirming audible sound on Vita. Record this
as user verification of audible audio on the tested device. It does not identify which session
the observation concerns, explain the earlier receive flag or prove that warning is erroneous.
There is no confirmed ongoing audio failure to justify changing the audio path. The new stream
also shows an IDR wait/request followed by resumed video decoding,
so its successful restart does not establish error-free media delivery. No runtime code
was changed for this log; no build or device test was performed by the agent.

### Mismatched server pin rejection and restoration — user and log verified, 2026-10-20

The maintainer tested a valid but incorrect saved server.pem, then restored the original
certificate. The supplied wrong-certificate attempt reached HTTP serverinfo but failed
connection with the expected security result (LAN address redacted):

```text
[GameStream] requesting server status
[GameStream] init failed for <host>: -10 (SSL peer certificate or SSH remote key was not OK)
[LegacyAdapter] ConnectHost failed result=-10
[ConnectionService] ConnectJob::Run result=-10
```

This is a TLS peer-verification failure, rather than the separate malformed-PEM load error.
HTTP metadata did not turn it into a successful connection; no app-list success or pairing
exchange follows in this failure excerpt. The test deliberately replaces the saved trust
anchor; it does not change Sunshine or exercise renewal of its live certificate.

After the maintainer restored the original pin, the log shows two serverinfo responses,
host persistence and successful authenticated app retrieval:

```text
[LegacyAdapter] ConnectHost ready
[ConnectionService] ConnectJob::Run result=0
[GameStream] applist count=4 total=4
```

No new pairing exchange appears in the supplied recovery log. This verifies pin mismatch
rejection and recovery with the original certificate on the tested Vita. It does not prove
the saved files were byte-for-byte unchanged by the failed attempt (no file hashes supplied),
nor cover malformed/unreadable pins, revoked client authorization, pin-write failure, two
hosts or cryptographic allocation/proof errors. Reported memory mode remains 2 with 65536 KiB
total and 6144 KiB newlib; installed package revision, firmware and Sunshine release are not
supplied. No runtime change or agent-run build/hardware test was required.

### Host-side revocation → PIN re-pair → Apps — user log verified, 2026-10-20

In response to the host-side revocation test, the maintainer supplied a connection log
showing a new PIN page, all pairing phases and successful authenticated app retrieval.
The transient PIN value is deliberately omitted from this record:

```text
[Pairing] text_pairing_status=... text=Enter this PIN in the Sunshine Web UI.
[GameStream] pairing phase=getservercert
[GameStream] pairing phase=getservercert response received
[GameStream] pairing phase=clientchallenge
[GameStream] pairing phase=serverchallengeresp
[GameStream] pairing phase=clientpairingsecret
[GameStream] pairing phase=pairchallenge
[Pairing] text_pairing_status=... text=Paired. Loading applications...
[GameStream] applist count=4 total=4
```

The new PIN page, final successful pairing and four apps verify recovery through PIN
pairing on this Vita. No saved-PC deletion or certificate replacement is shown. The
actual Sunshine revocation request and HTTP/XML authorization status are not present in
the client log, so the exact 401 fallback is source behavior rather than a captured wire
response. The earlier save_device_info paired=true is preserved stored metadata:
legacy_device_store_set_host_id updates identity/address without resetting that flag.
Moonlight API separately chooses READY versus PAIRED from the live server state, and
the observed PIN page shows that the cached flag did not bypass pairing.

This attachment stops after app retrieval. The maintainer subsequently replied “да всё ок”
to the requested full Moonlight restart and reconnect to Apps without another PIN. Record
that fresh-process reconnect after this specific re-pair as user-verified on 2026-10-20,
without a new log. Wrong-PIN rejection/retry, failed final challenge and persistence-failure
cases remain untested. The prior normal persisted reconnect test retains its original scope. Exact installed revision,
firmware and Sunshine release/configuration remain unspecified. No runtime code change,
agent-run build or agent-run device test was needed.

### Wrong PIN → semantic error → fresh retry — user and log verified, 2026-10-20

The maintainer deliberately entered a wrong PIN in Sunshine and subsequently identified
the installed release as **2025.924.154138**. Its UI reported
“Success! Please check Moonlight to continue”; Vita displayed an error and a new PIN.
The supplied client log shows (address, widget pointers and PIN omitted):

```text
[GameStream] pairing phase=getservercert
[GameStream] pairing phase=getservercert response received
[GameStream] pairing phase=clientchallenge
[GameStream] pairing phase=serverchallengeresp
[GameStream] pairing failed for <host>: legacy=-1 result=-1001 (Incorrect pairing PIN)
[Pairing] text_pairing_status=... text=Pairing with PC...
[GameStream] pairing phase=getservercert
```

Current L3 emits that PIN error only after verifying the signed server secret, when the
PIN-bound challenge hash differs. It returns before sending clientpairingsecret or
installing/persisting the candidate server pin. L1 maps it to the semantic wrong-PIN result
-1001; the PAF error-dialog completion path prepares a fresh PIN and starts pairing again.
The log plus maintainer report verify wrong-PIN rejection, correct classification, visible
error/new PIN and initiation of a new pending request. They do not yet verify successful
correct-PIN retry, wrong-signature/negative-EVP failures or certificate-file hashes.

The Sunshine message matches the source of the maintainer-reported release: submission
success is not proof of completed pairing. In the inspected
[v2025.924.154138 savePin](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/confighttp.cpp),
the admin response uses nvhttp::pin; that
[pin implementation](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/nvhttp.cpp)
releases getservercert and returns before later cryptographic phases. By contrast, the
[master implementation read on 2026-10-20](https://github.com/LizardByte/Sunshine/blob/master/src/nvhttp.cpp)
waits for a pairing-completion result, and
[master savePin](https://github.com/LizardByte/Sunshine/blob/master/src/confighttp.cpp)
returns that result. These branch URLs are moving references; the original pinned audit
baseline above is not retroactively changed. The maintainer subsequently confirmed the
installed version as 2025.924.154138, so this explanation is tied to that reported release
rather than inferred from the UI message alone. The package was not independently inspected.
Admin UI/API behavior remains reference context, not a new Vita admin-client feature. No client runtime change is needed for this successful
rejection test. The subsequent retry result is recorded below.

### Failed second request after wrong PIN — runtime failure, 2026-10-20

The next maintainer log supersedes any implication that retry recovery passed:

```text
[GameStream] pairing failed for <host>: legacy=-1 result=-1001 (Incorrect pairing PIN)
[GameStream] pairing phase=getservercert
[GameStream] pairing getservercert failed: -1 (Server returned nothing (no headers, no data))
[GameStream] pairing failed for <host>: legacy=-1 result=-1002 (Server returned nothing (no headers, no data))
```

The maintainer reports return to Main. This agrees with CURLE_GOT_NOTHING mapping to
NETWORK and the existing network-error dialog completion closing Pairing. Wrong-PIN
rejection remains verified; successful retry is now a reproduced compatibility gap.
The maintainer confirms this happens after submitting the new PIN in Sunshine. The
supplied Sunshine log contains UPnP and self-signed-certificate warnings, without a
pairing failure trace; it does not independently establish the cause of the empty HTTP reply.
Per maintainer decision, this is an accepted noncritical compatibility limitation (F6),
with no Sunshine patch or client workaround planned.

### Second PC: repeated errors and PIN replacement — reproduced, source fix pending Vita check

On 2026-10-20 the maintainer supplied another Vita log and reported testing a current
Sunshine on a second PC. Its exact version has been requested; do not assume the
original host's 2025.924.154138 version or equate "current" with an upstream commit.
Normal pairing on this PC completes all five phases and retrieves three apps. A later
incorrect-PIN attempt shows this separate sequence:

```text
pairing phase=serverchallengeresp
pairing failed: legacy=-1 result=-1001 (Incorrect pairing PIN)
[SceCommonDialog] Starting Init(), type 1
[SceCommonDialog] Term() completed, type 1
[Pairing] text_pairing_status=<widget> text=Pairing with PC...
pairing phase=getservercert
pairing failed: legacy=-1 result=-1003 (A pairing session with this uniqueid already exists)
```

The duplicate-session failures, dialog completion and new getservercert requests then
repeat. The maintainer observes the displayed PIN changing behind repeated error dialogs.
The log has no timestamps or dialog button results, so it does not establish whether
acknowledgements were manual or whether a dialog completed without input.

F7 below identifies the client retry loop. This is distinct from accepting the old
host's F6 compatibility limitation; fixing client failure navigation requires no Sunshine
patch or protocol workaround. The maintainer also reports delayed Web UI failure.
Sunshine [v2026.914.233613 nvhttp.cpp](https://github.com/LizardByte/Sunshine/blob/v2026.914.233613/src/nvhttp.cpp)
waits for handshake completion or a deadline in pin(), and rejects duplicate uniqueid
sessions with status 409. This explains a possible delay after the client rejects the
PIN before clientpairingsecret; the actual second-host version/timing remains unconfirmed.

### Single-dialog Search return after F7 — user/log verified on one Vita, 2026-10-20

The maintainer's follow-up to `8078c62` confirms the following sequence:

1. Enter an incorrect PIN: serverchallengeresp ends with -1001, one error dialog,
   manual OK and return to Search PCs.
2. Select that PC again before Sunshine displays its final failure: a new Pairing
   page/request receives -1003, "A pairing session with this uniqueid already exists".
   One error dialog ends this explicitly requested attempt and returns to Search again.
3. Sunshine displays "Pairing Failed: Check if the PIN is typed correctly" after a
   short delay (user observation, not timestamped in the Vita log). Another explicit
   PC selection creates a new PIN and reaches getservercert without an immediate error.

Each new pairing request in the supplied log follows another Search selection and
connection. The previous automatic retry/PIN-changing loop is absent in this tested
sequence. The second failure is a real host duplicate-session response; returning to
Search does not cancel that host session. This does not warrant a client retry timer,
identity rotation, invented cleanup endpoint or Sunshine modification.

The log ends at the third getservercert request. Its response, correct-PIN submission,
completed pairing and Apps retrieval after this recovery are not shown. Do not count
waiting with a new PIN as successful pairing. Exact second-host Sunshine version,
Main/manual entry and other error types remain unverified.

## 1. Scope and evidence

The client path remains PAF pages → MoonlightApp → services → MoonlightBackend →
LegacyMoonlightAdapter → Moonlight API / legacy GameStream / moonlight-common-c.

`implemented` means the required source path is present for the stated, limited scenario;
it does **not** mean device-verified. `partial` means a path exists with a concrete gap or limit.
`missing` means a required behavior has no effective path. `intentionally unsupported` means an
explicitly excluded client feature. `not applicable` means this is not a required client operation
or is not an endpoint of the reviewed server. Runtime evidence is stated separately for every row.

Historical user reports establish working connect/pair/list/stream paths and some stop/input behavior
on one Vita, as recorded in ROADMAP. They do not identify a Sunshine revision/configuration for this
audit, or prove the failure/reconnect cases below. **No new HTTP requests were made to a user's host,
no new Vita logs were supplied, and no Vita/Sunshine runtime case was executed in this audit.**

### Pinned upstream references

Public branch heads were resolved and source files downloaded read-only on the audit date.
These links are immutable snapshots, not a claim about all later releases.

- **S** — Sunshine `3d6e1d6008bd4a24906283ab25fcd0becc0a1c07` (master):
  [GameStream handlers, authorization and route registration](https://github.com/LizardByte/Sunshine/blob/3d6e1d6008bd4a24906283ab25fcd0becc0a1c07/src/nvhttp.cpp),
  [protocol version constants](https://github.com/LizardByte/Sunshine/blob/3d6e1d6008bd4a24906283ab25fcd0becc0a1c07/src/nvhttp.h).
- **SR** — [Sunshine RTSP/pending-session lifecycle](https://github.com/LizardByte/Sunshine/blob/3d6e1d6008bd4a24906283ab25fcd0becc0a1c07/src/rtsp.cpp).
- **SS** — [Sunshine streaming/control disconnect and session cleanup](https://github.com/LizardByte/Sunshine/blob/3d6e1d6008bd4a24906283ab25fcd0becc0a1c07/src/stream.cpp).
- **SA** — [Sunshine administrative routes](https://github.com/LizardByte/Sunshine/blob/3d6e1d6008bd4a24906283ab25fcd0becc0a1c07/src/confighttp.cpp).
- **SD** — [Sunshine Linux mDNS publication](https://github.com/LizardByte/Sunshine/blob/3d6e1d6008bd4a24906283ab25fcd0becc0a1c07/src/platform/linux/publish.cpp).
- **QH** — Moonlight Qt `f018a71a8a2a63377acde15366625e8effeae05b` (master):
  [HTTP/XML, server info, startApp, quitApp and TLS pin comparison](https://github.com/moonlight-stream/moonlight-qt/blob/f018a71a8a2a63377acde15366625e8effeae05b/app/backend/nvhttp.cpp).
- **QP** — [Moonlight Qt pairing](https://github.com/moonlight-stream/moonlight-qt/blob/f018a71a8a2a63377acde15366625e8effeae05b/app/backend/nvpairingmanager.cpp).
- **QS** — [Moonlight Qt session startup and DeferredSessionCleanupTask](https://github.com/moonlight-stream/moonlight-qt/blob/f018a71a8a2a63377acde15366625e8effeae05b/app/streaming/session.cpp).
- **C** — moonlight-common-c `aeba988e0a302145eff2e70fa3e11cd98ec01f5d` (master):
  [connection lifecycle](https://github.com/moonlight-stream/moonlight-common-c/blob/aeba988e0a302145eff2e70fa3e11cd98ec01f5d/src/Connection.c),
  [RTSP](https://github.com/moonlight-stream/moonlight-common-c/blob/aeba988e0a302145eff2e70fa3e11cd98ec01f5d/src/RtspConnection.c),
  [public callback/stop contract](https://github.com/moonlight-stream/moonlight-common-c/blob/aeba988e0a302145eff2e70fa3e11cd98ec01f5d/src/Limelight.h).
  The local submodule is **`2d984f4b0dfab5ea2cff0f262abe1a056946a7c1`**, not this head.
  Both versions were inspected; the reviewed termination/stop contract agrees. The upstream
  Connection.c also has a newer local-address-resolution step; no dependency update was attempted.
- **V** — xyzz/vita-moonlight `984603bd6f93f752593048fe494b5ffca14514e1` (vita):
  [libgamestream client](https://github.com/xyzz/vita-moonlight/blob/984603bd6f93f752593048fe494b5ffca14514e1/libgamestream/client.c),
  [XML parser](https://github.com/xyzz/vita-moonlight/blob/984603bd6f93f752593048fe494b5ffca14514e1/libgamestream/xml.c),
  [connection termination](https://github.com/xyzz/vita-moonlight/blob/984603bd6f93f752593048fe494b5ffca14514e1/src/connection.c).
- **N** — GrapheneCt/NetStream `99db706d860f7126b0b8b6ac1f5a5bbf960055f8` (**main**, not master):
  [AVPlayer::Term](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/players/player_av.cpp),
  [player-page lifetime](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/menus/menu_player_simple.cpp),
  [PowerTick registration](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/utils.cpp).
- **B** — Ibrahim778/BetterHomebrewBrowser `617ab7da240e33fca8a7f546a304f7e66cd87d45` (master):
  [page activation/stack ownership](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/617ab7da240e33fca8a7f546a304f7e66cd87d45/src/pages/page.cpp),
  [jobs and main-thread UI locking](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/617ab7da240e33fca8a7f546a304f7e66cd87d45/src/pages/app_browser.cpp).

N/B are PAF ownership references, not GameStream protocol authorities. N detaches the texture and
surface references before closing its player and scopes power ticks to playback; B deactivates and
reactivates pages and protects UI updates. These support preserving the current PAF/framebuffer
boundary while fixing transport ownership below it. Their player code is not a drop-in Moonlight teardown.

### Local implementation map

The 2026-10-20 follow-up adds [gamestream_xml.c](../src/moonlight/gamestream_xml.c) for
shared applist/launch/resume parsing; it does not move protocol logic into PAF pages.

- **L1** — [api.cpp](../src/moonlight/api.cpp): connect, pairing classification, app cache/parser,
  start/stop, deferred disconnect and API state.
- **L2** — [stream_session.cpp](../src/moonlight/stream_session.cpp): `load_launch_session`,
  `run_stream_session`, callbacks, setup/stop workers and cancellation flags.
- **L3** — [legacy_gamestream.c](../src/backend/legacy/legacy_gamestream.c): identity/certificates,
  HTTP setup, serverinfo, XML helpers and pairing handshake.
- **L4** — [legacy_moonlight_adapter.cpp](../src/backend/legacy_moonlight_adapter.cpp) and
  [services](../src/services): forwarding, jobs and cached adapter state.
- **L5** — [legacy_host_discovery.c](../src/backend/legacy/legacy_host_discovery.c) and
  [mDNS transport](../src/backend/legacy/mdns/udp_sniffer_vita.c).
- **L6** — local common-c [Connection.c](../third_party/moonlight-common-c/src/Connection.c),
  [RtspConnection.c](../third_party/moonlight-common-c/src/RtspConnection.c),
  [ControlStream.c](../third_party/moonlight-common-c/src/ControlStream.c),
  [SdpGenerator.c](../third_party/moonlight-common-c/src/SdpGenerator.c).
- **L7** — [Apps page](../src/pages/page_apps.cpp), [Stream page](../src/pages/page_stream.cpp),
  [MoonlightApp event dispatch](../src/app/moonlight_app.cpp).
- **L8** — [host store](../src/backend/legacy_device_store.cpp) and
  [legacy device persistence](../src/backend/legacy/device.c).

## 2. Protocol and authentication conventions

GameStream HTTP defaults to **47989**, HTTPS to **47984**; HTTP is supplied by discovery/manual
input and HTTPS is read from `HttpsPort`. RTSP normally uses **48010**, but `sessionUrl0` must be
preserved, including a nondefault port and `rtspenc://` scheme. These differ from Sunshine's admin
Web UI/API (normally 47990).

GameStream calls here are GETs with `uniqueid` (global client identity) and a request `uuid`.
The host UUID from `/serverinfo` is a different identity. Pairing bootstraps over HTTP with proof
derived from the PIN, then verifies the paired client via HTTPS. Subsequent application calls
use the client certificate/key; `uniqueid` alone is not authorization. In reviewed Sunshine,
HTTPS authorization checks the exact enabled paired X.509 identity. HTTP serverinfo reports
`PairStatus=0` and a placeholder MAC; authenticated HTTPS reports paired status/real MAC.

XML `<root status_code="..." status_message="...">` is application-level status, independent of
the HTTP status. A successful libcurl transfer is insufficient. `CURLOPT_FAILONERROR` does not
validate GameStream XML status. L3 uses Expat for status/fields; the F3 follow-up also uses a shared Expat parser for L1 app lists
and L2 launch/resume. Non-200 status cannot be accepted because a positive marker appears in the body.

## 3. Scenario matrix

Every entry below records endpoint/protocol, purpose, authentication, request/response semantics,
current implementation, upstream rationale, Sunshine behavior, status, and evidence/runtime needs.
Reference IDs resolve to the pinned source links above. **Verified: source** in an entry means the
local and referenced upstream branches were read, not that the scenario ran. Test IDs refer to §5.

### A01 — Discovery — partial

- **Endpoint / purpose / auth:** mDNS `_nvstream._tcp.local`, UDP 5353 multicast; find LAN hosts;
  no pairing required.
- **Semantics:** resolve service records/target/IPv4 address and advertised GameStream HTTP port;
  discovery results are temporary, not proof of availability or pairing.
- **Current:** L5 active query/sniffer, 10-second scan, maximum 16 entries, name/address/port
  deduplication. Main/Search now read dynamic service-sized snapshots and expose all available
  entries, including saved PCs beyond eight. Search's saved-record lookup covers all saved PCs
  by address/port; row tokens avoid pointers into vectors. Source checks pass; PAF/Vita acceptance
  remains open. The service's stale m_running cache/extra PAF start job are now removed: the
  backend owns the native worker/state, Start returns actual errors to Search's existing native
  dialog once, and Stop reaps workers after natural completion too. Restart joins the prior UID
  before replacement. HOST_SCAN_STARTED marks the attempt before worker startup; found/finished
  remain queued. Synchronous start errors do not also enqueue HOST_SCAN_FAILED.
- **Upstream / Sunshine:** SD publishes mapped HTTP port; V discovery is a reference for client
  discovery, N/B only for presenting results.
- **Verified / runtime:** source; T1 must verify repeated scans, >8 hosts, packet ordering and
  nondefault port. IPv6 discovery is intentionally outside the current IPv4 URL-builder scope.

### A02 — Server info — implemented (IPv4 connect-time snapshot)

- **Endpoint / purpose / auth:** HTTP/HTTPS `GET /serverinfo`; identify server and paired status;
  HTTP before pairing, paired client certificate for reliable HTTPS status.
- **Semantics:** XML status 200; required local fields `currentgame`, `PairStatus`, `appversion`,
  `state`; optional UUID, MAC, `HttpsPort`, `GfeVersion`, `ServerCodecModeSupport`.
- **Current:** L3 learns HTTPS port through HTTP. Without a saved server pin this is unpaired
  metadata only; a pin permits verified HTTPS. Only authorization status 401 allows a return to
  unpaired HTTP metadata; a TLS/protocol/transport error does not downgrade. L1 resolves the pin
  from the selected stored identity before any HTTP response, then persists UUID after connect.
  Missing required fields fail. Stream start always uses HTTPS, without fallback.
- **Upstream / Sunshine:** QH/V use serverinfo and BUSY-state normalization; S returns server
  metadata and process state. QH permits HTTP bootstrap without a pin and a 401 fallback; the
  current follow-up follows those trust boundaries. HTTP never establishes paired state (A22).
- **Verified / runtime:** source; T2 covers paired/unpaired, HTTPS denial, malformed response,
  missing optional fields, UUID identity and custom ports.

### A03 — Server capabilities — partial

- **Endpoint / purpose / auth:** serverinfo plus RTSP DESCRIBE/SDP; learn protocol/codec/audio
  capabilities under A02/A12 authentication.
- **Semantics:** version/codec masks and SDP are advertised capabilities, not a promise of successful
  capture/decoder setup. Local defaults are `SCM_H264` and, in L2's fallback, `7.1.431.0`.
- **Current:** L3 stores versions/codec mask; L2 advertises H.264, stereo, one controller,
  Rec.601 limited range, 6000 refresh rate x100 and packet size 1024. No general capability UI or
  server display-mode negotiation is implemented.
- **Upstream / Sunshine:** S computes codec flags; QH/QS use richer metadata; C negotiates SDP.
- **Verified / runtime:** source; T2/T4 verify actual negotiated mode and unsupported capture modes.
  HEVC/AV1, HDR, surround and multiple controllers are intentionally unsupported, not missing endpoints.

### A04 — Pairing preparation — implemented

- **Endpoint / purpose / auth:** local RNG, followed by HTTP `/pair`; show four-digit PIN for
  operator entry in Sunshine; host must be connected and unpaired.
- **Semantics:** `moonlight_api_prepare_pairing` returns a zero-padded PIN and emits
  PAIRING_REQUIRED; preparation itself makes no pairing HTTP request.
- **Current:** L1 uses Vita RNG; L4 PairingService queues the handshake. L3 loads/creates the
  installation-wide client identity under `ux0:data/moonlight/client/`.
- **Upstream / Sunshine:** V/QP use PIN challenges; S waits for host operator approval.
- **Verified / runtime:** source; T3 verifies fresh identity, already paired state and UI/job behavior.

### A05 — Pairing handshake — implemented (normal flow user-tested; error matrix pending)

- **Endpoint / purpose / auth:** HTTP `/pair` phases `getservercert`, `clientchallenge`,
  `serverchallengeresp`, `clientpairingsecret`; HTTPS `/pair?phrase=pairchallenge` proves client trust.
- **Semantics:** send salt/client certificate; receive hex server certificate; exchange AES-ECB
  challenges and signed secrets. Gen 7 uses SHA-256, older generations SHA-1. Check XML status and
  `paired=1` at each phase. Host operator supplies the displayed PIN through Sunshine UI.
- **Current:** L3 performs all phases, RSA-2048/signature-size assumptions, initial wait 300 seconds,
  later timeout 15 seconds; verifies signed proof before PIN mismatch classification, temporarily pins
  the authenticated server cert for the final HTTPS challenge, then saves server.pem. A failed final
  challenge rolls back the temporary pin. L1 keeps persistence failure distinct from protocol success.
- **Upstream / Sunshine:** QP/V use the same handshake family; S implements phased pairing,
  expiry and failed-session removal. QP likewise uses a temporary pin before the final HTTPS phase.
- **Verified / runtime:** normal PIN pairing and re-pair after the host-side revocation test are
  user/log verified on 2026-10-20 through all phases and authenticated applist (4/4). T3 still needs
  successful correct-PIN retry after a wrong PIN, delayed approval, expiry and final-challenge
  failure. Wrong-PIN rejection and a fresh pending retry are now user/log verified. Five-minute timeout is
  a client setting, not verified responsiveness on Vita.

### A06 — Pairing errors — partial

- **Endpoint / purpose / auth:** errors from A05; distinguish PIN mismatch, security, network,
  protocol and local failure without presenting storage failure as bad PIN.
- **Semantics:** XML rejection/`paired=0` differs from curl failure and cryptographic proof failure.
- **Current:** L1 recognizes PIN/security error strings, explicit certificate security failures,
  selected curl errors and OOM. Other local failures still fall through to PROTOCOL. The F4 source
  follow-up checks signature before the PIN-bound hash and accepts only successful EVP return codes.
  Tests exercise mocked return logic; actual malformed-proof/TLS runtime coverage remains pending.
- **Upstream / Sunshine:** QP has explicit failed/PIN/in-progress results; S can reject ordering,
  pending capacity, invalid proof or expiry. These are not all incorrect-PIN cases.
- **Verified / runtime:** user/log verified wrong-PIN result -1001 after serverchallengeresp,
  visible error/new PIN and a new getservercert request (2026-10-20). The subsequent retry
  after submitting the new PIN fails at getservercert with empty HTTP response/-1002 and page exit (F6), so
  recovery fails in this tested sequence. F6 is accepted as noncritical; no workaround is planned.
  F7 single-dialog return to Search and explicit re-entry are now user/log verified on one
  Vita (2026-10-20); the final correct-PIN recovery after host failure is not yet shown.
  Server XML status/reason are copied into pairing-failure events; the duplicate-session reason
  and Error 409 in the native dialog are user/log verified (F7). Other server codes/messages
  remain unverified.
  T3 still needs refused approval, lost network, malformed proof and local
  crypto/storage failures; semantic enum presence alone does not establish those paths.

### A07 — Application list retrieval — partial

- **Endpoint / purpose / auth:** paired HTTPS `GET /applist?uniqueid=...&uuid=...`; list launchable apps.
- **Semantics:** XML root status plus repeated `<App>` containing `<ID>`, `<AppTitle>` and optional
  capability data; status failure is not an empty successful list.
- **Current:** L1 reuses authenticated curl handle and refreshes a 64-entry global cache through
  L4 ApplicationService job. The 2026-10-20 source follow-up validates XML status and invalidates
  the cache on connect/disconnect/shutdown and failed refresh. Requests are rejected during stream
  setup/streaming/disconnect. L7 now owns a vector sized to the ready count and copies all available
  cached apps, with a shared backend bound of 64. The fixed eight-row/context arrays are removed
  in the separate 2026-10-20 presentation follow-up. Ten-row population is now log-verified
  on Vita; the maintainer reports the requested >8 check OK. The broader T4 matrix is pending.
- **Upstream / Sunshine:** S emits the configured app list; QH `getAppList` verifies status before
  XML parsing; V uses its XML parser.
- **Verified / runtime:** ten-app retrieval/population is user/log verified; the requested
  >8 selection check and normal switching between two PCs with different lists and back
  are user-reported OK (no new switch trace). The empty-state message without repeated
  requests or a hang is also user-confirmed. T2/T4 still require revoked client,
  concurrent/stress host changes; the 64/65 boundary remains unverified and is explicitly
  deferred by the maintainer (2026-10-20). Complete product support
  is not established by the 64-entry cache.

### A08 — Application list parsing — implemented (source; parser fixtures unrun)

- **Endpoint / purpose / auth:** A07 response, same paired HTTPS requirements; preserve app identity/title.
- **Semantics:** XML element boundaries, entity decoding, valid numeric IDs, malformed/truncated
  document rejection; entries must not inherit fields from unrelated/incomplete `<App>` elements.
- **Current:** `src/moonlight/gamestream_xml.c` uses Expat, checks the complete document/root status,
  decodes entities, validates integer IDs and requires fields within each App. Titles truncate at a
  UTF-8 boundary. DTDs, incomplete/malformed entries and duplicate scalar fields are rejected.
  Invalid output never becomes a valid cache.
- **Upstream / Sunshine:** QH/V use XML parsing; S serializes app titles as XML, including escaped entities.
- **Verified / runtime:** C grammar checks passed without compilation. `tests/gamestream_xml_test.c`
  contains actual-parser fixtures, but compilation/execution by the maintainer and Vita rendering
  remain pending. See tests/README.md; this is source implementation, not runtime acceptance.

### A09 — Launch — implemented (normal flow; compatibility/error tests pending)

- **Endpoint / purpose / auth:** paired HTTPS `GET /launch`; start selected host app and prepare stream.
- **Semantics:** query includes app ID, `mode=WxHxFPS`, SOPS, random `rikey`/`rikeyid`, local-audio
  choice, stereo surround info, controller bitmap and `corever=1`; reply has status and
  `gamesession=1`, normally `sessionUrl0`.
- **Current:** L2 `load_launch_session` sends these values with 10-second connect/20-second total
  timeout; the 2026-10-20 Expat follow-up requires root status 200 **and** gamesession=1.
  A present URL must be valid rtsp/rtspenc and fit the buffer; only an absent URL permits the legacy
  default-port fallback. App choice is checked against a fresh HTTPS snapshot (F2).
- **Upstream / Sunshine:** S refuses launch if an app is running and can report capture/launch errors
  in XML; QH verifies status, V checks status/session result. Qt allows a longer launch timeout (120 s).
- **Verified / runtime:** launch after closing the host app without reconnect is user/log verified
  on 2026-10-20: app=1093255277, accepted rtspenc URL, completed setup result=0 and decoded video
  through frame 841. T5 still needs slow startup, unsupported mode, failed capture, XML rejection
  and custom RTSP port. Timeout difference is a compatibility risk, not a proven defect.

### A10 — Resume — implemented (selected-app flow; race/error tests pending)

- **Endpoint / purpose / auth:** paired HTTPS `GET /resume`; attach to an existing host application.
- **Semantics:** fresh input key/id and stream options; successful reply `resume=1` and session URL.
  S resumes the **already running process**; supplied `appid` does not launch/switch it. No running
  app produces XML 503. Capture/encryption policy may also reject the request.
- **Current:** L2 chooses resume whenever cached `server->current_game` is nonzero, regardless of
  selected app in the audit baseline. The F2 follow-up now refreshes HTTPS state and rejects a
  different active ID (-8); it resumes only the selected running app. Same-app resume and different-app
  rejection are now user/log verified. Host-app exit without reconnect is also verified to select
  launch; other host-change cases and snapshot/request races remain pending.
- **Upstream / Sunshine:** S `resume`; QS asserts current app is zero or matches selection; QH
  validates response, V uses resume for a running app.
- **Verified / runtime:** different-app rejection user/log verified on 2026-10-20 (selected
  881448767, active 1093255277, result -8; see follow-up above). Same-app resume (1093255277) is
  also user/log verified through accepted rtspenc URL, completed RTSP/setup, connection started and
  decoded video. Closing the app on the host while Apps stays open is now also verified to choose
  launch and decode video (2026-10-20). Other host-change scenarios, snapshot/request races and the
  full reconnect matrix are not established by these three tests.

### A11 — Active game / session state — partial

- **Endpoint / purpose / auth:** `/serverinfo` snapshot (A02) plus local callbacks; distinguish host
  process, local transport and pending setup.
- **Semantics:** BUSY/currentgame describes host application state, not the number of active streams.
  S may retain an app after all clients disconnect. A positive local enqueue result is not STREAM_STARTED.
- **Current:** L3 normalizes `currentgame` to zero unless state contains `_SERVER_BUSY`; L1 tracks
  pending/active separately. L4 now changes stream state on lifecycle callbacks, not successful
  Start/Stop request return (source follow-up in F5). Disconnect now preserves the last state
  until post-cleanup CLOSED; API admission excludes reuse meanwhile. No ongoing host-state
  refresh or exposed durable snapshot. The connection enum
  does not expose pending/stop ownership; it must not replace the foreground/session guards.
- **Upstream / Sunshine:** QH normalizes BUSY; QS waits for callbacks; S uses `proc::proc.running()`.
- **Verified / runtime:** source; T5/T8 compare UI, API/adapter state, host app ID and actual transport.

### A12 — RTSP setup — implemented (delegated protocol path)

- **Endpoint / purpose / auth:** `sessionUrl0` from authenticated launch/resume; RTSP OPTIONS,
  DESCRIBE, SETUP audio/video/control, ANNOUNCE and PLAY. Session/input-key material comes from A09/A10.
- **Semantics:** C/L6 negotiate SDP, ports, session identifiers, codec/audio and encrypted RTSP;
  response codes/stages propagate through `LiStartConnection`. No separate handmade PAF handshake.
- **Current:** L2 forwards URL/version/codec mask/config to L6; real Vita video/audio callbacks are used.
  Missing URL fallback loses the server-selected encryption/port information (F3).
- **Upstream / Sunshine:** C handshake corresponds to SR handlers. `corever=1` causes S to return
  `rtspenc://`; L6 supports this automatically even though optional media encryption flags are NONE.
- **Verified / runtime:** source; T6 requires encrypted RTSP, custom ports, rejection/timeouts and
  cleanup at every startup stage. No interoperability claim for every firmware/server version.

### A13 — Stream start — implemented (H.264/stereo path)

- **Endpoint / purpose / auth:** A09/A10 → A12 → common-c control/video/audio/input transports;
  use negotiated session and key material, not Sunshine admin credentials.
- **Semantics:** `LiStartConnection` successful callback starts input and emits STREAM_STARTED;
  initialization failure produces stage failure and common-c cleanup.
- **Current:** L1 rejects duplicate active/pending start; L2 detached worker passes Vita renderers,
  emits failure when setup returns unsuccessfully; L7 opens the stream page on STARTED.
- **Upstream / Sunshine:** C/V/QS lifecycle; SR/SS create host capture/control workers.
- **Verified / runtime:** source plus historical working stream reports, not a new version-specific
  test. T6 covers stage failure, duplicate start, video/audio/input and repeated sessions; F1 affects
  termination after successful startup, not common-c's own failed-start unwind.
  A later 2026-10-21 trace identifies system build 98f0d44194af and verifies three ordinary
  same-app resumes/encrypted RTSP/setup=0, receiving audio/video and decoding 1833/1856/1840
  frames, with complete stop/release between starts (F5). This does not validate duplicate
  starts, stage failures or sustained streaming quality.

### A14 — Stream stop — implemented (explicit local stop path)

- **Endpoint / purpose / auth:** local `moonlight_stream_stop` → `LiStopConnection`; stop this
  client's transport. No new HTTP authorization exchange and no `/cancel`.
- **Semantics:** stop input, unwind common-c stages/threads, gracefully disconnect ENet control.
  `LiStopConnection` deliberately does not call `connectionTerminated`, so client emits STOPPED.
- **Current:** L2 persistent session owner consumes mutex-protected stop/termination requests and
  calls LiStopConnection after setup; repeated stop requests do not create another teardown owner. L7 detaches PAF surfaces and defers framebuffer release.
- **Upstream / Sunshine:** L6/C stop contract; S/SS observe control disconnect or timeout and
  clean stream workers; host app can remain running. QS separates transport stop and optional quit.
- **Verified / runtime:** source and historical double-PS stop report; T7/T8 still required for this
  full matrix, including immediate restart and final neutral input. No end-to-end idempotency proof
  under concurrent callbacks; L2 ownership flags are synchronized, while broader L1/L4 state races
  remain F5. F1 has a source owner implementation; full runtime acceptance stays open.
  Three ordinary double-PS stop/restart cycles on system build 98f0d44194af are now log-verified
  (2026-10-21): owner stop=0, audio drain/release=0 and three GXM unmaps=0 per cycle, with
  deferred release before the next Start (F5). No full concurrent/idempotency or neutral-host-
  input proof follows from these sequential cycles.

### A15 — Disconnect while idle/paired — implemented (normal idle cycle verified)

- **Endpoint / purpose / auth:** local DisconnectHost; drop local host connection/credentials handle;
  no remote quit/unpair request.
- **Semantics:** CONNECTION_CLOSED should mean local cleanup completed; does not remove saved
  metadata, revoke pairing or kill the host app.
- **Current:** L1 uses non-waiting selected-host admission, schedules cleanup, waits for the stream
  owner, frees L3 curl/crypto state, clears the host and emits CLOSED. L4 changes state on that
  event. Other host-control requests are rejected through worker release; duplicate pending
  disconnect returns success. No page caller of ConnectionService::Disconnect was found;
  closing Apps only restores the previous page callback. See the F5 source correction.
- **Upstream / Sunshine:** QS/V distinguish local termination from quit; S needs no special HTTP
  disconnect for an idle client. This is an API capability, not a tested UI disconnect feature.
- **Verified / runtime:** maintainer Vita log on 2026-10-21 verifies direct idle disconnect result 0,
  CLOSED/DISCONNECTED, reconnect to the same host without PIN, PAIRED and authenticated applist
  count=4. Apps rebuild removes all four old cells and inserts four. See F5 evidence below.
  Repeated/duplicate requests, admission races and cleanup worker creation failure still need T7;
  this single normal cycle does not close the failure matrix.

### A16 — Disconnect during pending launch — partial

- **Endpoint / purpose / auth:** local cancellation while paired HTTPS launch/resume is outstanding.
- **Semantics:** requesting stop cannot retract an HTTP request already accepted by the host.
  Cancel local continuation into RTSP; wait for the worker before freeing its server pointer.
- **Current:** L2 checks local cancellation after HTTP/before common-c. A source follow-up now
  publishes interrupt-safe entry in the first stage callback after common-c resets its flag,
  replays early cancel and sends one LiInterruptConnection while startup is pending. Cancellation
  before the STARTED commit suppresses input/event publication; issued interrupt results map to
  local -7 after owner unwind (including owner stop if common-c returns success). Actual raw
  errors are logged; uncancelled/pre-callback validation errors are preserved. L1 waits for the
  owner before freeing host state. Curl is not aborted. Stop after STARTED commits uses ordinary
  active teardown. Stage-4 interruption/unwind/recovery/final stop is now log-verified on one
  Vita. The later stage-10 run also verifies cancelled-success owner cleanup and FAILED buffer
  release before reconnect; other earlier-entry/late-stage races remain open (F5).
- **Upstream / Sunshine:** C offers `LiInterruptConnection` during pending common-c startup; SR
  expires pending launch session by configured `ping_timeout`. The remote process may still run.
- **Verified / runtime:** the 2026-10-21 maintainer Vita log verifies post-response/pre-RTSP
  cancellation (-7), CLOSED, native error dismissal, paired reconnect and a fresh four-app list.
  Immediate recovery FAILS at encrypted RTSP OPTIONS (-1); no STREAM_STARTED or PASS. Reported
  Sunshine 2025.924.154138 logs a matching RTSP tag-verification failure. See F5 for source
  analysis and remaining uncertainty. The later in-flight HTTPS test passes in the scoped run
  below. The old late-start teardown/recovery baseline and the later stage-4 interrupted
  partial-init unwind/recovery are now verified below. Blocked-I/O cancellation latency,
  earlier-entry/late-stage races, transport errors/timeouts, immediate-recovery reproducibility
  and the full cancellation/lifetime matrix stay open.
  A second host now has a log-verified PASS after a maintainer-reported approximately five-second
  pause before error OK: paired reconnect, same-app resume/RTSP, audio packets, 404 decoded frames
  and final stop/release on maintainer-reported Sunshine 2026.914.233613. Timing is unmeasured;
  see F5. This does
  not erase the immediate-retry failure or prove five seconds is sufficient generally.
  The later in-flight curl trace on host .134 verifies outgoing-header checkpoint → Disconnect=0
  → response/transfer result=0 → -7/CLOSED → paired reconnect/four Apps → same-app encrypted
  RTSP/setup=0/PASS, then decoded frames through 661 and incoming audio (F5). Current Sunshine
  version, client revision and error-OK timing are unspecified. The maintainer also confirms
  visible image, audible sound and double-PS stop; final release/unmaps are not logged. This is
  successful client-side in-flight ownership ordering, not curl abortion or remote rollback.
  The common-c stage-entry test now verifies Disconnect=0 → late STARTED → owner STOPPED/CLOSED,
  successful audio release/three old-buffer unmaps, paired reconnect/four Apps and encrypted
  RTSP/PASS late_start=1, then sampled decoded frame 301 (F5). The maintainer confirms image,
  audible sound and double-PS stop; recovery audio receipt/final release are not logged. This
  proves late-start teardown/recovery, not prompt cancellation.
  The later interrupt trace now verifies raw stage-4 -1 → audio/platform unwind → local -7/
  CLOSED, no first STARTED, paired reconnect/four Apps, encrypted RTSP/PASS late_start=0 and
  final double-PS stop with audio release/three unmaps. Recovery decodes 208 frames. The
  maintainer confirms audible sound despite the log's no-audio warning; this discrepancy is
  unresolved. This is stage-entry coverage, not blocked-handshake latency or late-init
  buffer cancellation. A subsequent stage-10 trace on maintainer-reported Sunshine
  2025.924.154138 verifies cancellation with three buffers/generation=1, suppressed frontend
  STARTED after raw common-c success, owner stop → -7/CLOSED → three successful old unmaps
  before reconnect → PAIRED/fresh Apps/same-app encrypted RTSP/PASS late_start=0 stage=10.
  Recovery receives audio/video and decodes 445 frames; final double PS releases audio and
  three buffers successfully. Image/sound are maintainer-confirmed. See F5 for limits; this
  single targeted cancelled-success path does not close the full matrix. Never substitute
  `/cancel` for local cancellation: its scope is host-wide (A20).

### A17 — Disconnect during active stream — partial

- **Endpoint / purpose / auth:** explicit local DisconnectHost uses A14 then A15; involuntary
  network/host termination arrives through common-c callback, with the existing session keys.
- **Semantics:** both routes need complete transport/resource teardown before reuse. Host-reported
  termination code zero differs from unexpected network/error termination.
- **Current:** explicit disconnect waits for L2 cleanup before freeing GameStream state. Since
  `0cef1ac`, involuntary termination queues a request for the same persistent session owner, which
  calls LiStopConnection before releasing ownership. One established-stream failure/restart is now
  log verified; the remaining F1 runtime matrix stays open.
- **Upstream / Sunshine:** V and QS both call LiStopConnection on termination cleanup; C's callback
  signals termination, it does not perform its caller's stop. SS handles disconnect/timeouts host-side.
- **Verified / runtime:** unexpected ENet disconnect, complete owner teardown, deferred buffer
  release and manual resume with decoded video are log verified on 2026-10-20. This is one
  established-stream failure/restart. Separately, the 2026-10-21 maintainer log identifies system
  build f25e735baf54 and verifies explicit active DisconnectHost → owner stop result=0 → STOPPED
  → CLOSED/DISCONNECTED → paired reconnect → same-app resume/RTSP → PASS, 654 decoded frames,
  incoming audio packets and final manual stop with three successful unmaps in each teardown.
  See F5 for playback confirmation and memory limits. Host kill, repeats/failures and setup races
  still need T7/T8.

### A18 — Reconnect — partial

- **Endpoint / purpose / auth:** reconnect serverinfo, then paired applist and fresh launch/resume/RTSP;
  reuse installation identity but generate new session keys.
- **Semantics:** wait for prior teardown, refresh host state; app may persist, exit or change while away.
- **Current:** L1 rejects connect during worker/disconnect activity and refreshes metadata on new
  ConnectHost. Follow-ups F1/F2/F3 retain cleanup ownership, refresh HTTPS state before every start
  and invalidate the app cache at host transitions. Each new connection loads the host server pin;
  old records without it require pairing. Broader async state risks remain F5.
- **Upstream / Sunshine:** QS serializes active session cleanup; S retains process independently of
  stream and accepts resume with fresh session material.
- **Verified / runtime:** normal stop/restart, app-exit launch, same-app resume and manual resume
  after unexpected control disconnect are user/log verified on 2026-10-20. Normal two-PC
  app-list switching and return are subsequently user-confirmed without new logs. Direct idle
  DisconnectHost → CLOSED/DISCONNECTED → ConnectHost/PAIRED → fresh applist count=4 is also
  maintainer-log verified on 2026-10-21 (F5). The separate f25e735baf54 active test also verifies
  CLOSED → paired reconnect → fresh four-app list → same-app resume/RTSP and decoded video
  through 654 frames. The maintainer separately confirms visible image and audible sound
  after reconnect for this exact run (2026-10-21). A later in-flight HTTPS cancellation run
  also verifies paired reconnect, fresh four Apps, same-app encrypted RTSP/PASS, 661 decoded
  frames and incoming audio. The maintainer also confirms visible image, audible sound and
  ordinary double-PS stop for that run; final release/unmaps are not logged (F5). A subsequent
  common-c startup late-start teardown/recovery trace verifies STOPPED/CLOSED, old audio/buffer
  release, paired reconnect/four Apps and same-app encrypted RTSP/PASS through 301 decoded
  frames. The maintainer also confirms image, audible sound and double-PS stop for that run;
  recovery audio receipt/final release are not logged. The later interrupted-startup run now
  verifies -7/CLOSED, paired reconnect/fresh Apps, same-app encrypted RTSP/PASS late_start=0,
  208 decoded frames and final normal stop/audio release/three unmaps. Audible sound is
  maintainer-confirmed despite a contradictory no-audio log warning; the discrepancy stays open.
  The later stage-10 cancellation/FAILED release test verifies old-buffer release before
  reconnect, PAIRED without PIN, fresh four Apps, same-app encrypted RTSP/PASS and 445 decoded
  frames with incoming audio, then final double-PS stop/audio release/three unmaps. The
  maintainer confirms image/sound and Sunshine 2025.924.154138 for this run. It does not
  retrospectively explain the older audio warning or establish immediate-recovery timing.
  Three subsequent ordinary same-app stop/restart cycles on system build 98f0d44194af verify
  repeated reuse of the existing paired host, encrypted RTSP and received audio/decoded video,
  with complete local release before each next Start. No new ConnectHost/CLOSED occurs;
  repeated host reconnect and shutdown remain separate (F5).
  Pending-launch failure variants, concurrent host/cache races, repeated failure cycles and
  remaining T8 cases stay open.
  Automatic reconnect here is opt-in test behavior; production network autoreconnect is not implemented.

### A19 — Host-side session cleanup — partial (end-to-end evidence pending)

- **Endpoint / purpose / auth:** ENet control disconnect/timeout and RTSP launch expiry; no extra
  client HTTP endpoint is required just to release stream resources.
- **Semantics:** distinguish pending launch slot, established stream workers, retained application
  process and display/controller state. Releasing one is not proof the others were reset.
- **Current:** L6 explicit stop gracefully disconnects; pending launch cancellation sends no host
  request. The F1 session owner also performs transport teardown after unexpected termination;
  pending launch expiry and host resource release still need runtime observation.
- **Upstream / Sunshine:** SR discards pending launch events after `ping_timeout`; SS stops on
  control disconnect or ping timeout and joins workers/reset input. It can retain the running app;
  display restore and gamepad retention depend on host configuration/session state.
- **Verified / runtime:** source; T7/T8 inspect Sunshine logs, pending/active sessions, input, app
  process and display separately. Pending-event expiry alone does not prove a launched app was killed.

### A20 — Cancel / quit semantics — intentionally unsupported (remote Quit action)

- **Endpoint / purpose / auth:** paired HTTPS `GET /cancel`; explicitly quit the remote application.
  `/quit` is not a registered GameStream route in S.
- **Semantics:** S calls `terminate_sessions()`, terminates the running app and reverts display
  configuration, returning XML `cancel=1`/status 200. It is not per-request cancellation and can
  affect other clients. QH's method named `quitApp()` actually sends `/cancel` and rechecks serverinfo.
- **Current:** no remote Quit UI/service/backend operation; StopApplication only stops local stream.
- **Upstream / Sunshine:** S cancel handler, QH quitApp, QS optional quit-after-graceful-stop, V gs_quit_app.
- **Verified / runtime:** source; T9 only if an explicit remote-Quit feature is later requested.
  Do not call this on ordinary stop, network loss or pending-launch cancellation. No endpoint added.

### A21 — Unpair / delete saved host — missing (requested client self-revocation)

- **Endpoint / purpose / auth:** legacy GameStream HTTP `/unpair` exists in V/QP error-cleanup paths;
  S has **no registered `/unpair` route**. Sunshine paired-client revocation is administrative.
- **Semantics:** QP/V may issue best-effort unpair after failed pairing; that does not establish
  compatibility with S. S removes failed/expired pairing attempts itself. Admin client removal is
  a separate authenticated operation and revokes the selected server-side client identity.
- **Current:** L8 DeleteHost removes exact local host metadata only, preserving global identity;
  no remote unpair or credential deletion. On 2026-10-19 the user requested remote unpair on Delete,
  then explicitly declined administrative login. The requested self-revocation is therefore
  **missing**, with an upstream protocol prerequisite; it is no longer intentionally excluded.
- **Upstream / Sunshine:** QP/V cleanup calls; S pair expiry/route table; SA registers
  `POST /api/clients/unpair` and unpair-all. An older API comment says `/api/unpair`; the route table
  is authoritative for this pinned revision. The admin handler calls `authenticate`, accepts a JSON
  server-side client `uuid`, and returns `status`; that generated UUID is not the client's uniqueid
  or host UUID. GameStream pairing credentials do not authorize this admin request.
- **Verified / runtime:** source; T3 tests failed-pair retry; T9 revokes client on host and tests
  reconnect. Rechecked current Sunshine master on 2026-10-19: it still resolves to S and has no
  client self-unpair route. Current Moonlight Qt `e4db5c2c18e0a55fd8a33d8d735d8b71c1f9a710`
  [pairing implementation](https://github.com/moonlight-stream/moonlight-qt/blob/e4db5c2c18e0a55fd8a33d8d735d8b71c1f9a710/app/backend/nvpairingmanager.cpp)
  retains legacy best-effort `/unpair` error cleanup; that is not a Sunshine route or normal Delete.
  No live host request was made. Local Delete is not evidence that Sunshine forgot the client.
  Admin API remains out of scope by the user's explicit choice. Implementation requires a supported
  authenticated client self-revocation operation or an agreed host extension; no fake endpoint,
  `/cancel`, or global-credential removal is an acceptable substitute. The plan/acceptance checks
  are recorded in ROADMAP under requested remote self-unpair.
  Rechecked 2026-10-21 against current Sunshine
  [GameStream routes](https://github.com/LizardByte/Sunshine/blob/master/src/nvhttp.cpp) and
  [admin handler/routes](https://github.com/LizardByte/Sunshine/blob/master/src/confighttp.cpp):
  still no GameStream self-unpair; admin unpair authenticates separately and checks CSRF.
  This is a source recheck, not a request to the maintainer's host or a host-extension proposal.

### A22 — Certificates / authentication — partial

- **Endpoint / purpose / auth:** global client RSA certificate/key on HTTPS; pairing proof establishes
  server identity. Client UUID and host UUID have separate roles (§2).
- **Semantics:** authenticate client to server **and** check the paired server certificate, allowing
  known self-signed identity without accepting arbitrary peers. Certificate replacement/revocation
  must cause re-pair/error, not silent trust.
- **Current:** L3 loads global Vita identity and the selected host's server.pem separately.
  VERIFYPEER is enabled with an OpenSSL certificate-verification callback comparing the entire
  leaf certificate to the PIN-authenticated pin. DNS/CA trust is replaced by this exact paired
  identity; hostname checks remain disabled for LAN IPs/self-signed certificates. HTTPS without
  a pin is forbidden. Redirects, TLS session caching and connection reuse are disabled.
  New pins are persisted only after the final authenticated pairing exchange. Missing pin means
  re-pair; unreadable/malformed pin or mismatch fails closed without HTTP fallback. The explicit
  recovery is local Delete → Add → PIN pairing; global client credentials remain unchanged.
- **Upstream / Sunshine:** QP pins the learned certificate and returns it for persistence; QH
  compares peer certificate on SSL errors; S validates the exact enabled client X.509 identity.
- **Verified / runtime:** valid-but-mismatched saved server certificate rejected with -10; after
  restoring the original, connection and authenticated applist (4/4) succeed. User/log verified
  on 2026-10-20; see follow-up above. Normal pairing and persisted reconnect were also user-tested.
  Re-pair after the host-side revocation test is now user/log verified through a new PIN, all
  phases and authenticated applist (4/4). Fresh-process reconnect after this specific re-pair is
  subsequently user-confirmed without another PIN (2026-10-20; no new log). T3/T9 still need malformed/unreadable pins, live host-cert renewal, damaged credentials,
  failed persistence, successful correct-PIN retry after rejection, malformed-proof/crypto errors
  and remaining revocation variants. Wrong-PIN rejection with result -1001 and fresh retry
  initiation is now user/log verified (2026-10-20).

### A23 — Protocol / version compatibility — partial

- **Endpoint / purpose / auth:** serverinfo versions/codec mask → launch `corever` → RTSP/SDP;
  A02/A09/A12 authentication.
- **Semantics:** protocol generation and feature bits drive transport behavior; Sunshine release
  version is not GameStream appversion. S advertises `7.1.431.-1` and GFE `3.23.0.74` at this revision.
- **Current:** L3 has nominal generation 3–7 limits, but L1 calls init with `unsupported=true`,
  bypassing those checks. L6 still parses versions/selects transports; arbitrary future versions
  are not thereby supported. `corever=1` enables encrypted RTSP. Optional A/V encryption flags NONE
  do not disable common-c's required/negotiated encryption or remote-input key usage.
- **Upstream / Sunshine:** C feature negotiation; S make_launch_session and launch/resume encryption
  policy checks. Passing version defaults is not a negotiated capability result.
- **Verified / runtime:** source; T6/T10 cover pinned/latest deployed Sunshine, custom ports,
  encryption policies, missing/bad metadata and any claimed older GFE support. No release-wide
  compatibility range or mandatory-encryption interoperability is hardware-certified here.
  For the wrong-PIN run, the maintainer explicitly identified Sunshine 2025.924.154138; the
  PIN-submission response is compared with that exact release source above. This does not
  establish support for every configuration or silently identify versions of earlier runs.

### A24 — App artwork / administrative API — intentionally unsupported / not applicable

- **Endpoint / purpose / auth:** paired GameStream HTTPS `/appasset?appid=...` provides image data;
  separate admin HTTPS `/api/*` manages Sunshine configuration/clients/operator approval.
- **Semantics:** artwork is optional for the current text-only app list. Admin authentication is
  distinct from the GameStream client certificate/PIN handshake.
- **Current:** no artwork request is intentionally supported; no admin client requirement exists.
- **Upstream / Sunshine:** S registers `/appasset`; QH has app-asset retrieval; SA owns admin routes.
- **Verified / runtime:** source. Artwork is **intentionally unsupported**; admin integration is
  **not applicable** to this audit. No runtime test is required for absent features in the current flow.

## 4. Concrete findings and minimal separate fixes

These are source findings, not runtime fixes. Runtime code must be changed as separate, reviewable work.

### F1 — Unexpected termination cleanup ownership (established-stream failure/restart verified)

**Current source, 2026-10-20:** `0cef1ac` already introduced a persistent session owner, mutex-protected
stop/termination requests and owner-thread `LiStopConnection()`. The finding below records the older
audit baseline. Do not implement it again. On 2026-10-20 the supplied Vita log verifies established
control disconnect → owner-thread common-c stop/cleanup → deferred framebuffer release → manual
resume with successful setup and decoded video. The terminal -1 is the original connection error,
not a teardown failure. Setup-cancel/race cases, full host disconnect, repeated cycles and other
T6–T8 failure modes remain pending; see the runtime follow-up above.

**Root cause:** L2 `on_connection_terminated()` calls `vita_input_stop`, emits STREAM_STOPPED,
then clears `s_stream_context.started`. Once the setup worker has returned,
`moonlight_stream_is_running()` becomes false. L1 clears active/pending on that event; later
StopApplication rejects it, stream_stop returns early, and disconnect/shutdown can free the server
without ever calling `LiStopConnection()`. L7 only closes the page/releases framebuffers.

**Reference:** local L6 and C require explicit LiStopConnection for stage/thread/resource cleanup;
their termination shim invokes the client callback on a separate thread. V calls LiStopConnection
in connection termination; QS always does it in deferred session cleanup. The callback notification
does not unwind the established common-c connection.

**Minimal direction:** retain a distinct transport-owned/cleanup-pending state; serialize explicit
stop, remote termination, failed setup and disconnect through one cleanup owner. Stop input, finish
LiStopConnection once setup has returned, then expose terminal state/event and allow reconnect.
Keep PAF page/surface detachment and decoder-buffer release ordered as required by L7; do not move
transport code into pages or merely force the UI to refresh. No `/cancel` is needed.

**Regression risks:** simultaneous manual stop/termination, callback/setup return race, duplicate
STOPPED events, freeing decoder buffers while PAF references them, reusing global common-c state
before cleanup. `volatile` flags do not serialize this. Validate T6–T8, including the callback before
the setup worker has fully returned. This finding is sufficient to design a fix; no new hardware
success is claimed. Pending launch cancellation fixes already in master should be preserved.

### F2 — Resume can target a different app or stale process state

**Source follow-up, 2026-10-20:** the session worker now fetches HTTPS serverinfo immediately before
launch/resume, without HTTP fallback, checks pairing and compares the active ID to the selection.
A different app returns `LEGACY_GAMESTREAM_APP_CONFLICT` (-8) through the stream event; it is never
resumed or cancelled automatically. Same-app resume and idle launch remain distinct. Cancellation
is checked around the refresh. Current-game numeric input and BUSY suffix are validated. The host
can still change state after the snapshot; XML errors remain authoritative. Different-app rejection
is user/log verified on 2026-10-20: selected=881448767, active=1093255277, setup result=-8. Same-app
resume is also user/log verified on that date: app=1093255277, accepted rtspenc URL, completed
RTSP/setup, connection started, setup result=0 and decoded video through frame 901. Host-app exit
without reconnect is additionally verified on that date: launch app=1093255277, accepted rtspenc URL,
setup result=0 and decoded video through frame 841. The three basic decision branches now pass on
the tested Vita; other host-change scenarios, snapshot/request races and remaining T5/T8 cases stay open.
The root cause below describes the older baseline.

**Root cause:** current_game is read at connect/pair time, then locally overwritten with selected
application_id after start. L2 does not refresh before deciding launch versus resume or compare the
selection to the host's active ID. S's resume ignores selection for process switching.

**Reference:** QS asserts active app matches selected app; S resume requires an existing process,
while S launch rejects one. A still-open client can therefore resume app A after the user selects B,
or request resume after A exited. Simply clearing current_game on local stop would also be wrong:
the remote process may still run.

**Minimal direction:** fetch authenticated current state immediately before start, keep actual
host app ID separate from requested ID, allow same-app resume, and report a different active app
without silently switching. A future explicit Quit-and-launch flow needs its own UX and scope decision.
Host state can still change after the snapshot, so preserve XML rejection/error reporting.

**Regression risks / tests:** extra request latency, app exit between snapshot and launch, two clients,
older GFE BUSY semantics; T5/T8. Do not introduce a destructive `/cancel` as an automatic fallback.

### F3 — XML error responses can become successful list/launch results

**Source follow-up, 2026-10-20:** implemented a shared Expat parser for applist/launch/resume,
root status plus verb-result validation, entity decoding, per-App fields, strict IDs, URL validation
and cache invalidation. Shared HTTP transfers now reinstall their own callback/buffer/timeout under
a mutex, including shutdown exclusion; app-list no longer inherits the previous launch callback.
This fixes request-buffer ownership, not all asynchronous API state races (F5). The later separate
Apps presentation change removes the eight-row limit through a page-owned vector; the backend
bound remains 64. C grammar checks passed; C parser fixtures and the broader UI Vita matrix remain pending.
The root cause below describes the audit baseline.

**Root cause:** L1 marks app cache valid after any curl-successful body. L2 accepts an OR of
status text/session markers and uses RTSP default if no URL. Malformed XML, non-200 root status
with a positive marker, or status 200 with a negative session marker can be misinterpreted.
Title entities/incomplete App fields are mishandled; cache survives host transitions.

**Reference:** L3 already has Expat status/field helpers; V validates root/session fields and QH
validates root status with real XML parsing. Sunshine routinely returns protocol errors in XML.

**Minimal direction:** isolate status/app-list/launch parsing using existing Expat, validate full
document and the verb's session result, propagate status/message, decode entities, reject truncation,
and invalidate cache on host transitions. Preserve optional URL fallback only where required for
documented older hosts; a malformed modern response must not silently lose encryption/custom ports.
Parser and scalable UI storage should remain separate changes.

**Regression risks / tests:** valid empty list versus failure, legacy optional fields, integer/title
limits and partial documents; T4/T5. The original inventory made no parser changes; see the dated
source follow-up above for implementation and still-unrun C fixtures.

### F4 — Server TLS identity is not pinned; signature error check is too broad

**Source follow-up, 2026-10-20:** signature return handling and security-before-PIN ordering are
now implemented. Each EVP stage must return 1; a negative final crypto error cannot become boolean
success. Pairing clears stale attempt errors. The non-build `tests/test_pairing_verification.py`
interprets the actual C expression AST for 27 mocked EVP combinations and checks ordering; it does
not run OpenSSL, compile C or verify Vita behavior. The original finding below describes the
audit baseline, not the corrected source.

**Server pinning source follow-up, 2026-10-20:** maintainer approved per-host `server.pem` storage.
The host store supplies the path through the Moonlight API; the selected stored identity is resolved
before HTTP bootstrap. The final pairing exchange uses a temporary verified pin and rolls it back
on failure; only success persists it through a temporary file and rename. Write errors do not turn
protocol success into pairing failure and cannot silently establish persistent trust.

HTTPS uses VERIFYPEER with an exact-leaf OpenSSL verification callback, not ordinary public-CA
trust. Callback installation/backend support failures reject connection. The inspected VitaSDK
headers report libcurl 8.17.0 and OpenSSL 1.0.2i; the callback supports both the 1.0.2 public context
and modern opaque-context accessor. Actual linked library/backend behavior is not established by
headers. The implementation rejects a non-OpenSSL curl backend rather than disabling verification.
References: [libcurl SSL context callback](https://curl.se/libcurl/c/CURLOPT_SSL_CTX_FUNCTION.html),
[OpenSSL custom certificate verification](https://docs.openssl.org/1.1.1/man3/SSL_CTX_set_cert_verify_callback/),
and QP/QH above. This overrides chain/CA/name/time validation with the paired identity, so the same
paired certificate remains trusted even if its validity dates expire; a changed certificate needs
explicit re-pair. Connections are fresh and not reused, preserving callback/pin lifetime.

Pairing and shutdown are mutually excluded to protect client keys and temporary pins across HTTP
phases. This does not claim to solve every API/service race (F5). Old records without a pin are
unpaired until PIN proof succeeds; malformed/unreadable pins and TLS mismatch do not downgrade.
Deleting a host also removes server.pem through existing directory cleanup, without host-side unpair.

Six non-build Python checks now include 12 actual-expression cases for present/absent/equal/unequal
certificates with mocked X509 comparison, enrollment ordering/rollback and C grammar checks. They
do not exercise a TLS handshake, file I/O, X509 implementation, compilation or Vita. T3/T9 remain
required: old record re-pair, persisted trust after restart, changed/malformed/missing pin, two hosts,
revoked client, and failed pin writes. The later Vita pin-mismatch test on 2026-10-20 now verifies
one of these rejection paths: wrong valid certificate → security result -10; restoring the
original → connection result 0 and applist count=4 total=4. Normal pairing/persisted reconnect
were previously user-confirmed. Re-pair after the host-side revocation test is now also user/log
verified through a new PIN, all phases and app retrieval (4/4); fresh-process reconnect after
this specific re-pair is subsequently user-confirmed without another PIN (no new log). Remaining error cases are not covered by these results.

**Root cause:** L3 authenticates the client certificate but disables server certificate verification.
The verified pairing server certificate is discarded. In `verify_signature`, logical `&&` coerces
`EVP_DigestVerifyFinal` to boolean, so a negative crypto error also becomes true when prior calls
succeeded; the later `result == 1` does not repair that conversion. Pairing compares PIN hash before
checking signature, unlike QP, weakening error attribution on an untrusted response.

**Reference:** QP's temporary/persisted server pin, QH peer-certificate comparison and QP's
`verifySignature` requiring a positive return; S enforces client authorization separately.

**Minimal direction:** first isolate the signature return comparison (`== 1` before boolean
composition) and security-before-PIN classification with focused crypto/error fixtures. Plan server
certificate pin storage and renewal/re-pair semantics explicitly through host/backend ownership;
this is an authentication feature and requires a deliberate persistence contract, not an incidental
format change. Standard CA-only verification would reject normal paired self-signed hosts.

**Regression risks / tests:** OpenSSL Vita version, malformed signatures, self-signed certificates,
host certificate renewal and existing host records; T3/T9. The negative-return logic issue is proven
from source semantics; no exploit or device failure was demonstrated.

### F5 — Sequential foreground workflow, operation state and lifecycle coverage

**Maintainer direction, 2026-10-20:** implement sequential user workflows: one foreground
operation, native WAIT for long steps, no conflicting actions, terminal result/cleanup,
then the next step. Pairing retains the visible PIN page and explicit cancellation.
GameStream control calls already block inside jobs/workers; keep PAF main-thread dialog
and lifecycle processing responsive. Streaming's ongoing transport/render threads remain.
The async event mechanism is internal completion delivery, not a product requirement for
parallel foreground workflows. The source-risk findings below still apply to ownership
and late completion; the proposed direction is a small serialized operation model.

**Baseline root cause:** L4 eagerly changes adapter state on enqueue/stop/disconnect; L1/L2 workers and
callbacks update globals without a synchronization owner. L7/L4 event delivery can lag and
MoonlightApp's 16-event queue drops overflow (now logged by the follow-up below). ConnectionService exposes Disconnect but
no page calls it. Closing a PAF page is not necessarily protocol disconnect.

**Minimal direction:** keep durable backend operation state and terminal cleanup ownership, update
observable state on actual completion, make important event loss diagnosable, and use a backend-level
test trigger for currently unreachable disconnect scenarios. Do not add a new disconnect UI only
to make an audit row green. Respect N/B page-lifetime patterns and main-thread dispatch.

**First source implementation, 2026-10-21; build/Vita pending:** ConnectionService now owns
reservation and native WAIT for Connect, app-list loading and launch/setup. Reserve before
starting work, reject overlap/WAIT initialization failure, and terminate WAIT before forwarding
completion to the page. Connect/app-list jobs retain their return result with release/acquire
publication, preventing completion before the backend call returns and providing a fallback
when their queued event is lost. Zero apps is a successful result. Ownership is released before
dispatch so a successful connection may immediately enter Apps/loading. Enqueue/start failures
close WAIT before releasing the reservation. Base blocks page opening/ordinary Back while busy.
Connect retains its native error dialog; app-list/launch failures retain their page status.

This reuses the existing native connection WAIT/termination path without changing any endpoint
or protocol semantics. Pairing keeps its visible PIN/cancellation path and the stream retains
its own worker/cleanup owner. Queue overflow now logs event/result, but launch/pairing terminal
events lack the job-result fallback. Stop/Disconnect APIs are not serialized by this gate;
late events, shutdown and broader backend/cache races remain open. No universal F5 closure
or device verification is claimed. Check `[Workflow]` operation 1/2/3 handoffs on Vita.

**UI follow-up, 2026-10-21:** the maintainer reports the wait window appeared only during
launch after `e838d91`; Connect/Apps still showed inline progress. The source still had those
page status calls. They are now removed/hidden for foreground progress; ConnectionService
shows the operation text in SceMsgDialog USER_MSG/NONE instead of generic system WAIT.
This changes presentation only, with the same reservation/completion ownership and no
protocol changes or artificial delay. Earlier short-step dialog visibility is not explained
by the supplied observation alone. Build, no-button dialog appearance and handoffs remain pending.

**Page-text policy follow-up, 2026-10-21; source only:** Main/Search/Apps error notifications
now use the existing service-owned native OK dialog, with immutable displayed text and
log-only suppression of concurrent notifications. Connection errors have one service owner;
the page no longer repeats the failure text. Pairing retains its native semantic/server error
dialog and OK → parent navigation; failure to show it is log-only. Pages contain static
empty-state labels and pairing instructions instead of dynamic progress/result/error strings.
Invalid-address IME retry waits for the native error to close. These changes do not alter
GameStream endpoints, pairing semantics, cancellation or the stream statistics overlay.
Native presentation, error handoffs and retry regression tests remain Vita/build pending.

**Progress visibility follow-up, 2026-10-21:** maintainer accepts the static pairing layout
but sees only the stream-start dialog. The supplied log shows successful Init/Term and
completion for Connect (result 0) and app list (four apps), followed by successful resume/setup.
It has no timing or presentation evidence for the first two windows. Source review found normal
completion used unconditionally unchecked Abort. ConnectionService now uses checked native
Close → FINISHED → checked Term, retrying a close request when initialization has not yet
reached RUNNING. The installed message-dialog reference samples use normal Close for no-button
and wait messages; this does not establish visibility behavior on firmware. Open/first-poll/
close/term elapsed diagnostics were added, without artificial delay or protocol changes.
The missing presentation root cause and effect remain runtime pending; pairing appearance
acceptance is not acceptance of these windows. See UI.md for the focused follow-up.

**Compact PAF busy-window follow-up, 2026-10-21; source only:** later timings show first PAF
poll at 15/2 ms, close requests at 1750/1531 ms and Term at 2602/2383 ms for Connect/Apps.
The maintainer still sees late/brief windows and selects BHBB's native `s_dialogTextSmallBusy`.
ConnectionService now checks/loads CommonGuiDialog and owns a PAF Show/Close slot for the three
foreground steps. Backend result ownership and event dispatch remain; no GameStream request,
pairing or streaming protocol is changed. The initial implementation released the logical slot
on a nonnegative PAF Close return; this incorrect interpretation is corrected below.
BHBB itself does not check the return. Native PAF owns visual cleanup,
without SceMsgDialog FINISHED/Term polling. Errors/Delete
still use SceMsgDialog. Pending checks include native appearance/latency, input blocking,
success/error handoffs and shutdown on Vita. No automatic visibility or speed claim is made.

**Compact wait acceptance, 2026-10-21:** maintainer reports good appearance. The system-profile
trace embeds `ca359a25edf0` and verifies operation 3/slot 2 completion after native Close returns
0, then StreamPage, successful launch/setup, received audio/video and decoded frames. PS double
tap ends the stream, followed by successful three-buffer unmapping. First Close returned
`0xEE251690`, with the second return 0 317 ms later; its semantics/root cause are unknown.
Do not decode it as a known SCE error or claim the retry proves a permanent fix. No new
Connect/Apps dialog trace, failure/shutdown/normal-profile matrix or build transcript is supplied;
inclusion of the later header fix cannot be inferred from that embedded revision. F5 remains
open beyond the accepted appearance/launch-stop path. Runtime code is unchanged for this record.

**Close-contract investigation, 2026-10-21:** initial source/header and binary-metadata checks
did not establish the meaning of the return. ScePaf-RE declares int32_t without result semantics;
BHBB/NetStream call once and ignore it. Our negative-result retry was a project assumption.
The two available core dumps omit the function body; that stage required a decrypted native
module and made no runtime changes. The subsequently supplied ELF establishes that Close
leaves internal register values, including a stack guard, rather than a signed status code.
The client now calls Close once, discards the return and releases its logical slot, matching
BHBB/NetStream; the negative-result retry is removed. No GameStream protocol code changes.
See docs/UI.md for file hash, export/import mapping, return paths and regression risks.
Eight existing non-build checks pass; build and Vita handoffs/errors/shutdown remain pending.

**Post-correction Vita trace, 2026-10-21:** all three normal foreground steps now have exactly
one close-request line before completion: Connect slot 0 at 1767 ms/result 0, Apps slot 1 at
1547 ms/result 4, setup slot 2 at 5187 ms/result 0. Four app rows are inserted; same-app resume
and RTSP/setup succeed, then StreamPage opens. This verifies the normal logical handoffs on
one Vita without the former return-based retry. It does not measure visual animation completion
or cover error dialogs, failed job start, shutdown, repeated sessions or broader F5 races.
The trace has no build-revision marker. Explicit PS-double-press stop completes result 0,
restores the infobar and unmaps all three video buffers successfully. Sustained playback is
not verified: 42 decoded frames, subsequent IDR wait/request and no audio traffic reported;
user observation is needed before diagnosing a playback issue. No runtime code, build or
agent device test accompanies this documentation update.

**Maintainer clarification, 2026-10-21:** in reply to the moving-image/sound question, the
maintainer reports “всё нормально”. Normal observed playback in this run is user-confirmed.
The IDR/audio log warnings remain unexplained; this report does not establish a playback defect
or justify streaming changes. Longer-session quality and the remaining F5 cases are unchanged.

**Diagnostic cleanup, 2026-10-21:** after that acceptance, the maintainer requests removal of
temporary diagnostics. Workflow timing/success traces, Connect/Pairing job-lifetime pointers,
successful enqueue and Pairing PIN/widget traces are removed, together with timing-only state.
Failure/fallback logs remain; historical evidence above is retained. No protocol, cancellation
or dialog-lifecycle behavior changes. Eight existing non-build checks pass; the agent did not
build or run on Vita for this cleanup. See docs/UI.md for the exact scope.

**Adapter Start/Stop state correction, 2026-10-21; source only:** a focused review of the
remaining F5 state problem finds a reproducible ordering defect: StartApplication assigns
STREAMING after `moonlight_api_start_application()` returns 0, even if the setup worker has
already emitted STREAM_FAILED/STOPPED. It overwrites the callback's PAIRED state. Without a
callback yet it also reports streaming before transport establishment. StopApplication similarly
reports PAIRED immediately after accepting a stop request, before owner cleanup/STREAM_STOPPED.

Minimal correction: remove those two request-return assignments. Existing ForwardEvent handles
STREAM_STARTED → STREAMING and STREAM_STOPPED/FAILED → PAIRED; synchronous failures keep their
return code and do not mutate adapter state. While setup is pending the last state remains PAIRED;
while stopping an active stream it remains STREAMING until the terminal event. These values are
observed lifecycle state, not authorization to start another operation; service reservation and
L1/L2 pending/active guards remain authoritative. No new pending enum, UI, endpoint or log is added.

This follows V's connection-start callback, QS's callback/deferred-cleanup separation and
[common-c's callback contract](https://github.com/moonlight-stream/moonlight-common-c/blob/aeba988e0a302145eff2e70fa3e11cd98ec01f5d/src/Limelight.h):
start success is a connectionStarted event; explicit LiStopConnection does not generate
connectionTerminated, so L2's existing post-cleanup STREAM_STOPPED remains necessary.
The installed source flow through L1/L2 was inspected; no stream-owner change is required.

`tests/test_adapter_lifecycle.py` interprets the actual two method bodies and ForwardEvent with
mocked API calls. Ten accepted-request/event orderings (callback before/after return), six
synchronous failure cases and both no-host guards are checked. Before correction seven ordering
cases fail; after it all eleven Python tests pass. This is non-build serial source interpretation,
not C++ ABI/type checking, real-thread synchronization, hardware playback or event-loss testing.

DisconnectHost is deliberately a separate follow-up: its early host flag clear currently blocks
new Start/Pair requests. Removing it alone would permit reuse while deferred cleanup runs;
pending-disconnect admission/serialization must be established first. Adapter/API shared-state
synchronization, stale events and unexposed DisconnectHost runtime cases remain F5 gaps. Next
device regression: normal start/stop, fast setup failure then retry, and pending-setup stop;
observe return to Apps/new start without a false streaming/idle state. No agent build/device run.

**Maintainer follow-up:** after the Start/Stop correction, the maintainer reports
“я проверил, всё работает”. This accepts the observed smoke-test behavior on their Vita.
No new log, installed revision or individually enumerated fast-failure/pending-stop cases
were supplied; do not extend this confirmation to those interleavings or all F5 coverage.
The next independent task remains pending-disconnect admission/ownership.

**Selected-host admission / disconnect correction, 2026-10-21; source only:** the preceding
next task is now implemented. The root gap was inconsistent admission: Connect and applist
checked disconnect flags, but prepare-pairing and stream start could enter during cleanup.
Flag checks alone also left a check/use window against credential release and cached-list copy.
The adapter reported DISCONNECTED/cleared its host when the request returned, before cleanup.
The worker emitted CLOSED before clearing the API host flag, exposing unfinished bookkeeping.

L1 now scopes Connect, pairing preparation/handshake, both app-list paths, Start and Stop with
a project-owned `HostControlRequest`. It uses `pthread_mutex_trylock`, so a competing request
returns -1 immediately. Disconnect uses the same admission; if another synchronous control
call owns it, disconnect returns busy without acceptance. An in-flight pairing must first be
cancelled through its existing token and allowed to return; no new blocking/abort path is added.
Settings/discovery and stored-host metadata APIs keep their existing independent paths.

The accepted disconnect worker holds admission while waiting for the stream owner and freeing
GameStream state. This protects server/credential/cache lifetime against the guarded callers;
it does not serialize the ongoing transport callbacks or solve all shared stream-state races.
The atomic pending flag supports duplicate disconnect returning 0 while cleanup owns the mutex.
It is published only after pthread creation succeeds, under the request lock; a creation/stop
failure returns its error, releases admission and produces no false CLOSED. If a stop was already
accepted before worker creation failed, transport stop can still finish; disconnect can be retried.

After cleanup the worker sets DISCONNECTED and clears the API host before CLOSED. Admission
stays closed through callback publication, then worker/pending flags reset before unlocking.
Callbacks must queue frontend work; a synchronous reentrant host request receives busy, and
must not spin/wait in the callback. Shutdown waits for admitted work/cleanup, releasing the
mutex before yielding to a scheduled cleanup worker that has not acquired it yet.

L4 no longer writes state/host after accepted Disconnect. Connect state/host publication moves
to its synchronous CONNECTION_READY handler (reading L1's READY/PAIRED state); pairing uses
PAIRING_FINISHED. This prevents a returning Connect/Pair wrapper from overwriting a CLOSED
event after L1 releases admission. The unused adapter host copy is removed; persistent host
identity/storage and the public state enum are unchanged. No new UI, endpoint or runtime trace.

Reference basis remains C/QS/V: explicit local stop/owner cleanup is separate from host quit,
and completion follows cleanup. The trylock guard is a project-specific admission mechanism
around the existing protocol; it is not a copied Moonlight/Sunshine API requirement. N/B's
main-thread event/PAF boundaries are preserved. No Sunshine administrative calls are introduced.

All twenty non-build Python tests pass, including source-interpreted admission/entry/cleanup/
shutdown paths and adapter callback-before/after-return cases. Mutex/thread/atomic operations
and scoped-request destruction are mocked; these checks do not compile C++, run real pthreads,
prove race freedom or validate curl/crypto lifetime on Vita. Installed VitaSDK declares trylock;
no build/device test was run by the agent. Regression coverage still needed: normal Connect/
pairing/apps/start/stop after the gate, direct idle/pending/active DisconnectHost, immediate
reuse, duplicate requests, failure rollback and shutdown. Direct disconnect currently needs a
backend-level trigger; closing Apps is not that test. A15–A18 and full F5 stay partial/pending.

**Maintainer smoke-test follow-up:** following the selected-host admission change, the
maintainer reports “всё работает”. This is user-reported normal application success, without
a new log, installed revision marker or enumerated steps. It does not establish direct
DisconnectHost execution (no current UI caller), pending/active disconnect, rollback or shutdown
coverage. Keep A15–A18/F5 runtime requirements open; no runtime code changes accompany this record.

**Idle runtime trigger source follow-up:** `MOONLIGHT_DISCONNECT_TEST=ON` adds a one-shot
scenario after the first selected host's Apps response. It calls ConnectionService::Disconnect,
requires CLOSED/DISCONNECTED, reconnects to the captured host/port through the normal Connect
job and requires PAIRED plus a fresh application-list response. Hooks run only on the main
thread after normal completion gating; frontend callbacks remain intact. PASS/FAIL markers
use Debug log only in this test package. No game is launched and no remote quit/unpair occurs.
The option defaults OFF; build.sh resets it OFF per invocation unless explicitly enabled, so a
cached test option does not silently carry into an ordinary script build. No permanent UI or
stored setting is added. User action/identity change, backend error or step timeout aborts the
scenario's further actions. See [maintainer steps](../tests/runtime/README.md) for the exact
build command and console sequence. Build, transfer and Vita execution remain maintainer work.
This supplies the previously missing idle trigger; pending/active disconnect and the broader
F5 matrix remain open, and preparation of a trigger is not runtime acceptance.

**Idle runtime acceptance, 2026-10-21 (maintainer Vita log):** the opt-in scenario now reports
PASS. The captured target is 192.168.1.134:47989. After the first authenticated four-app list,
Disconnect returns 0, CLOSED is observed with adapter state DISCONNECTED, and reconnect to
the same target returns PAIRED without another PIN exchange. A separately enqueued app refresh
returns count=4 and the test reports `PASS idle CLOSED -> reconnect -> applications count=4`.
The normal Apps callback then logs `before=4 after_delete=0 after_insert=4`, confirming the
nonempty list rebuild as well. No FAIL or backend error appears in the supplied sequence.

Memory samples identify AppMgr mode=2 with total=65536 KiB, newlib=6144 KiB and PAF=10240 KiB;
USER_RW free is 33876 KiB at both connection-ready samples. Newlib used changes from 94352 to
93944 bytes while its arena grows from 221184 to 245760 bytes. One cycle does not establish
absence of leaks. The fragment contains no launch revision/profile marker or Sunshine version;
those exact installed versions are not independently established by this evidence.

This closes the normal direct idle disconnect/reconnect check on one Vita. Pending-launch and
active-stream disconnect, duplicate/admission races, rollback, shutdown and repeated/stress
cycles remain unverified. No runtime code change or agent-run build/device test accompanies
this acceptance. The next separate scenario is explicit DisconnectHost during an active stream,
with owner teardown before CLOSED and reconnect/resume afterward.

**Active-stream trigger source follow-up, 2026-10-21; build/Vita pending:** the missing
trigger, rather than a demonstrated new cleanup bug, is the next A17/F5 validation gap.
`MOONLIGHT_ACTIVE_DISCONNECT_TEST=ON` selects a separate test-only implementation of the
existing main-thread hooks. It cannot be combined with the idle mode; both are OFF by default
and reset OFF by build.sh unless explicitly requested. No protocol/backend or PAF-page change.

The maintainer starts an app from Apps. On its actual STREAM_STARTED the test captures app ID,
waits three seconds by elapsed-time polling, then invokes service Disconnect once. It requires
successful STOPPED for that host/app before CLOSED/DISCONNECTED, then reconnects to the exact
captured host/port, requires PAIRED, refreshes Apps and requests the same app ID. PASS requires
a second successful STREAM_STARTED/STREAMING, not start acceptance. The stream is left active
for visual/audio verification and ordinary manual stop. STOPPED is allowed to arrive through
the queue after the adapter has already reached DISCONNECTED; event ordering is checked without
assuming backend state pauses at each queued event. Existing frontend callbacks and deferred
framebuffer release run normally.

Reference basis is the existing C/QS/V/S audit: common-c Limelight.h documents explicit stop
separately from connectionTerminated and forbids overlapping transport lifecycle calls; L2 emits
STOPPED after its owner has called LiStopConnection, then L1 frees GameStream and emits CLOSED.
The test exercises this ownership contract rather than implementing host cleanup through a new
endpoint. Remote Qt/Sunshine reference retrieval was unavailable during this follow-up; no new
upstream behavior is claimed. Fresh serverinfo still chooses resume versus launch. The test
PASS alone cannot establish `/resume`, decoder output, audio or host resource cleanup: inspect
the normal resume/RTSP/owner/frame-unmap logs and confirm playback on Vita. It does not send
host-wide `/cancel`, quit or unpair requests.

All 34 non-build Python checks pass; source-interpreted cases cover the active flow and failures with mocked services,
time and queued events. Both implementations remain excluded from ordinary builds. Risks:
wrong host/app replay, premature restart, a stale/missing terminal event and abandoned automatic
work on error. The scenario rejects mismatches/failures, requires CLOSED and a fresh list before
restart, times out automatic steps and issues no further test commands after PASS/FAIL. It does
not cancel outstanding backend work on timeout or intercept controls. Initial user selection
has no test deadline. This is not real concurrency/ABI or device proof. Run the separate active
[maintainer procedure](../tests/runtime/README.md#active-stream-scenario);
A17/A18 and full F5 remain partial. Pending launch, rollback, shutdown and stress remain separate.

**Active-stream runtime follow-up, 2026-10-21 (maintainer Vita log):** the supplied launch marker
identifies `profile=system system_app=ON commit=f25e735baf54`, AppMgr mode=2, 65536 KiB total,
6144 KiB newlib and 10240 KiB PAF. The test captures 192.168.1.134:47989 and app=1093255277.
The first stream has successful RTSP/setup and decodes 162 frames before the requested stop.

The session owner stops input/audio/video/control, drains/releases audio with result=0 and
finishes with result=0. Then the test observes STOPPED for the selected app followed by
CLOSED/DISCONNECTED. All three old framebuffers unmap with result=0 and deferred release
finishes after ConnectHost begins but before the next stream setup. Reconnect reaches PAIRED
without another PIN; authenticated applist returns four entries and Apps rebuild logs
`before=4 after_delete=0 after_insert=4`. The second Start uses the same app ID and explicitly
logs `resume app=1093255277`, an accepted rtspenc URL, successful RTSP handshake/setup and
STREAM_STARTED. The test reports PASS. Video continues through 654 decoded frames; audio
traffic first arrives at 4600 ms. A subsequent double-PS stop request completes owner
cleanup with result=0 and another three successful framebuffer unmaps. No test FAIL appears.

The first three-second session logs no received audio traffic; the resumed session does receive
audio. Audio initialization/packets do not establish audible playback; decoded frames do not
independently establish visible presentation. The maintainer subsequently confirms for this
exact run (2026-10-21): “Да, изображение и звук работают” after automatic reconnect. This is
user-confirmed visible/audible playback in addition to the log evidence. ENet interrupted/control
failure messages occur inside both requested stop sequences, followed by acknowledged
peer disconnect and successful owner completion; they are not a failed test result.

USER_RW free is 33880 KiB before the first connection, 33600 KiB after reconnect and 33600 KiB
after the final stop/deferred release (280 KiB below the initial sample). Both stream-after-setup
samples report 10832 KiB free. These are different lifecycle points in one cycle, not evidence
of a leak or of leak freedom; repeated settled-baseline testing is still pending. Sunshine's
exact release/configuration is absent; `server=7.1.431.-1` is the reported protocol version,
not identification of a Sunshine package release. Input logs show motion=0; this does not
verify motion or the broader input lifecycle. No host-side resource trace is supplied.

This verifies the direct active disconnect/paired reconnect/same-app resume event sequence,
video decoding, user-confirmed image/sound and subsequent manual stop on this one Vita/build.
A17/A18/F5 retain open failure/race/stress and pending-launch coverage. Next prepare a separate
pending-launch DisconnectHost scenario (A16). No runtime code change or agent-run build/device
test accompanies this evidence record.

**Pending-setup trigger source follow-up, 2026-10-21; build/Vita pending:** the remaining
A16 gap is an untested cancellation/lifetime path, not a newly reproduced runtime bug. A fixed
delay after Start could accidentally test idle or established streaming; the test instead uses
a precise hook after load_launch_session succeeds and before the existing RTSP cancellation
check. MOONLIGHT_PENDING_DISCONNECT_TEST=ON compiles one separate scenario and two hooks:
service Start captures the accepted app on the main thread, and the stream worker publishes
an atomic checkpoint then yields until released (maximum 60 seconds, no stream/control mutex
held). PAF poll calls the existing service Disconnect while foreground Launch is still busy,
then releases the worker. The worker checkpoint performs no PAF work; service/UI commands
remain on the main thread.

The real Disconnect sets cancellation; the unchanged post-response check must return -7.
The test requires STREAM_FAILED=-7 for the chosen host/app before CLOSED/DISCONNECTED and
rejects any first STREAM_STARTED or STOPPED. The normal Apps callback still displays its
native launch-error dialog; the user presses OK once. Automatic reconnect waits until that
service-owned dialog is no longer busy, requires PAIRED without a PIN, refreshes Apps and
requests the same app ID once. Only the recovery STREAM_STARTED/STREAMING produces PASS.
The worker gate cannot hold the recovery a second time. This is a post-HTTP/pre-RTSP scenario,
not cancellation of in-flight curl or common-c startup, and not proof of remote launch rollback.

Reference basis remains A16 and the already inspected C/QS/V/S implementations. Vendored
common-c Limelight.h requires a pending LiStartConnection to finish before starting another;
this checkpoint is earlier than that call, so it uses the existing cancellation flag/owner
and waits for CLOSED rather than introducing LiInterruptConnection or a new endpoint. L2
publishes its failed completion before clearing the owner-running flag, and L1 waits for that
owner before freeing GameStream state. An already accepted host launch may persist; the
normal fresh serverinfo chooses resume/launch on recovery. Any host pending-session rejection
is logged as a test failure; no host patch, cleanup workaround, timed retry or `/cancel` is added.

All 44 non-build Python checks pass. Tests interpret actual scenario functions with mocked
atomics, time, services and serial interleavings; they also interpret the real post-response
cancel fragment (error bypass, -7 and uncancelled continuation). They cover busy launch,
failure-before-CLOSED, wrong host/app/state/result, no early STARTED, native-error busy gating,
one-shot recovery, timeouts and release on failure/shutdown. They do not compile C++, prove
atomic ordering or real concurrency/curl/crypto lifetime, validate native error presentation
or execute the new path on Vita. Main failure/shutdown releases the gate; its worker deadline
prevents an indefinite test hold if PAF disappears. Releasing the gate does not itself cancel
setup: backend lifecycle remains the owner. User selection and error OK have no test deadline.

All three disconnect modes default OFF and are mutually exclusive; build.sh resets them OFF
before explicit overrides. Ordinary builds contain neither new hook nor source. The only
runtime-file edits are guarded test hooks; protocol/cancellation logic, storage and frontend
callbacks remain unchanged. Risks to validate: checkpoint/native-dialog interaction, teardown
ordering, host pending-session behavior and recovered playback. See the exact
[pending test procedure](../tests/runtime/README.md#pending-setup-scenario).
A16/A18/F5 remain partial; curl wait, RTSP/common-c cancellation, rollback, shutdown and stress
still require their own coverage.

**Pending runtime failure, 2026-10-21 (maintainer Vita + Sunshine logs):** app=1093255277
reaches the post-response checkpoint, Disconnect returns 0, and the original setup exits -7
before RTSP. The test receives that failure followed by CLOSED/DISCONNECTED. The ordinary
native error opens/terminates; reconnect reaches PAIRED, authenticates and rebuilds four Apps
rows (`before=4 after_delete=0 after_insert=4`). Thus the test did continue after OK.

A second Start is accepted, sends a fresh resume and receives an accepted rtspenc URL. Its
RTSP stage 4 fails on the OPTIONS response: `Failed to decrypt RTSP response`, error=-1.
Common-c unwinds audio/platform initialization; setup returns -1 and the test logs
`FAIL phase=10` (AwaitRestarted). No STREAM_STARTED, decoded video or PASS is present.
The maintainer identifies Sunshine **2025.924.154138** and supplies its matching line:
`[2026-10-21 12:57:23.132]: Error: Failed to verify RTSP message tag`.
This is a failed immediate stream recovery, not a missed reconnect or an error-OK stall.
The Vita fragment has no Launch revision marker; its checkpoint diagnostics match the prepared
test, but the installed revision is not independently identified. USER_RW free is 33876 KiB
at both connection-ready samples; one cycle does not establish memory safety or leak freedom.

**Version-specific source comparison:** [Sunshine v2025.924.154138 RTSP](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/rtsp.cpp#L413-L469)
retains one pending launch event until timeout, associates new RTSP sockets with it and declines
to replace it on another raise. Its encrypted-message handler reports tag failure and responds
using that session's cipher. [The versioned resume handler](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/nvhttp.cpp#L843-L929)
constructs key material from the new request and still returns HTTP success when queue
admission silently retains an older event. Live master RTSP/nvhttp inspected on 2026-10-21 has the same
relevant behavior; no newer-host runtime result is claimed.

Local L2 generates fresh key/IV per start and passes that material to common-c. This agrees
with [Moonlight Qt's per-session key generation](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/session.cpp#L643-L647).
Vendored Connection.c copies the new StreamConfig and RtspConnection.c creates fresh cipher
contexts. Source-supported explanation: cancellation leaves pending session A/key A; the next
resume sends key B, while RTSP still binds to A. Both the server request-tag failure and client
response-tag failure fit that mismatch. This is an inference from matching versioned source
and both logs, not captured proof of the exact selected session/key; no secret material or
session-ID trace was supplied. The log does not diagnose certificate pinning or a PAF delay.

The minimal current action is to record the failed interoperability scenario and preserve its
FAIL result. No concrete wrong client key construction, premature teardown or supported
client-only pending-session cleanup is established here. Existing scope excludes Sunshine
patches and client workarounds; no sleeps/retries, key reuse, encryption downgrade or host-wide
cancel are introduced. A16/A18/F5 remain partial. Host pending-session expiry followed by a
new explicit attempt is unverified and can distinguish this explanation without changing the
client. The [versioned default configuration](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/config.cpp#L479-L480)
sets ping_timeout to 10 seconds, configurable in milliseconds. With that unchanged default,
wait at least 15 seconds after FAIL, keep this process open in Apps and select the same app
once. The one-shot test is Finished, so that explicit attempt will not be held/cancelled again.
If customized, wait longer than the actual configured timeout. Record resume/RTSP/video/audio;
this is a diagnostic check, not an implemented timed retry. No build, runtime-code change or
new hardware run was performed by the agent.

**Delayed error-OK recovery, 2026-10-21 (second-host maintainer log):** the maintainer reports
analogous behavior on the latest Sunshine, then a successful restart after waiting about five
seconds before pressing the first cancellation error's OK. The supplied successful trace uses
another host, 192.168.1.154 / server UUID 166F8DA0-16E7-2A25-7E83-62FF30CC202C, with one app,
2072647516. The maintainer subsequently identifies Sunshine **2026.914.233613**. The client
Launch revision is absent. “Latest” is the user's description, not independently established
release data. The immediate-failure behavior on this host is user-reported; this trace contains the delayed
successful run. It is not the earlier proposed manual selection after a completed FAIL.

The first accepted resume reaches the checkpoint, Disconnect returns 0, and setup returns -7
before RTSP. The test observes cancellation then CLOSED. After error dismissal, reconnect
returns PAIRED without PIN, applist count=1 and Apps rebuild `before=1 after_delete=0 after_insert=1`.
The automatic second Start uses the same app and explicitly logs resume, accepted rtspenc URL,
completed RTSP stage 4, setup result=0 and PASS. Audio packets arrive; video decodes 404 frames.
IDR waiting/drop/recovery appears and decoding resumes afterward; this is not an acceptance
of sustained stream quality. Double-PS stop finishes owner cleanup with result=0, audio drain/
release result=0 and three successful framebuffer unmaps. No test FAIL appears in this run.
The maintainer calls the restart successful; visible/audible playback is not separately itemized.

USER_RW free is 33880 KiB at both connection-ready samples and 33600 KiB after final deferred
release; this single cycle does not establish leak freedom. The delay is approximate and
reported by the maintainer, not measured from timestamped logs. Other setup/network/dialog
time adds to the pending-session age. The host identity/version also changed, so these runs
are not a controlled timing-only comparison. Success is consistent with pending-session expiry,
but neither exact expiry time nor five seconds as a safe universal threshold is established.

Record this narrow delayed-OK cancellation/reconnect/resume cycle as passed on the second
host. Preserve the immediate-recovery FAIL and its tag-mismatch analysis; A16/A18/F5 remain
partial. No hardcoded wait/retry, key/encryption change, Sunshine patch or runtime-code change
is added. The earlier post-FAIL explicit retry remains unrun. Next independent A16 coverage is
cancellation while the HTTPS request is outstanding, separately from this post-response hook.
No build/device run was performed by the agent.

**In-flight HTTPS trigger source preparation, 2026-10-21 (before maintainer run below):** the outstanding
A16 gap is cancellation while curl still owns the HTTP transfer, distinct from the prior
post-response checkpoint. MOONLIGHT_HTTP_DISCONNECT_TEST=ON reuses the pending main-thread
controller/barrier with an HTTP-specific log prefix and callback adapter. The earlier
post-response hook is excluded in this mode. The runtime edits are guarded test hooks only;
no new endpoint, protocol cancellation mechanism, user-facing UI or storage change.

L2 arms the selected app immediately before HTTPS launch/resume. Under L3's HTTP mutex, the
adapter installs a private curl debug sink only for that armed operation. A matching outgoing
GET header ending in CRLFCRLF enters the checkpoint before response-header processing; other
verbs, internal handles, recovery attempts and a response observed before the checkpoint cannot
trigger it. The main poll requests the real service Disconnect while foreground Launch remains
busy and the transfer/buffer/mutex are live, then releases the worker. Curl completes naturally;
existing parse/cancel logic must report -7, then the owner exits and deferred host cleanup emits
CLOSED. The user dismisses the ordinary -7 error, followed by paired reconnect/Apps/same-app
Start. No automatic delay or retry is added to hide the known pending-session RTSP failure.

The debug sink reads only fixed request-line/header-end markers and records metadata; trace
buffers/URLs/keys are never printed or retained. It is installed before enabling verbosity;
verbosity is disabled before callback/data removal on completion or setup failure, while the
HTTP mutex still protects the curl handle and stack context. Body/write callbacks, pairing
progress, TLS trust and timeouts remain unchanged. This exercises HTTP ownership, not immediate
wire-level cancellation. A timeout/transport error keeps its actual failure and cannot count
as a successful -7/recovery sequence. The common worker gate still has failure/shutdown release
and a 60-second safety deadline; user app selection and error OK remain user-paced.

Reference: [curl DEBUGFUNCTION contract](https://curl.se/libcurl/c/CURLOPT_DEBUGFUNCTION.html) and
[8.17.0 request send/HEADER_OUT order](https://github.com/curl/curl/blob/curl-8_17_0/lib/request.c#L201-L208).
Installed SDK headers identify 8.17.0; callback ABI/behavior in the actual linked library is
unverified. This is a libcurl callback test seam, not a copied Moonlight/Sunshine cancellation
API. Existing C/QS/V/S ownership references still apply: await the old worker before reuse;
local Disconnect does not roll back an accepted remote launch or invoke host-wide quit.
The peer may already have generated/buffered its response while the client is held. The test
therefore covers client-side in-flight ownership before response processing, not a measured
server delay or loss/retransmission case. Split/unrecognized callback markers may miss the
checkpoint and fail/incomplete the test; they cannot substitute post-response coverage.

All 62 non-build Python checks pass: shared controller in both modes, interpreted C callback/
option handling, wrong verb/handle, no early response coverage, one-shot availability, setup
failure cleanup and actual hook ordering. C grammar checks include all original trust/HTTP/
pairing fragments plus the new adapter/header. Atomics, buffers, options, services and scheduling
are mocked/serial: no real curl/TLS, C/C++ type/ABI, concurrency, hardware or leak proof. No
build/device run by the agent. All four test modes default OFF and are exclusive; normal builds
exclude the adapter and hooks. See the exact
[HTTP test procedure](../tests/runtime/README.md#in-flight-https-scenario).
A16/A18/F5 remain partial; real in-flight cleanup, transport failures, recovery playback,
RTSP/common-c cancellation and broader repeated/shutdown cases still need device evidence.

**In-flight HTTPS maintainer Vita result, 2026-10-21:** the new `[DisconnectHttpTest]`
trace targets host 192.168.1.134, server UUID 3D7D0EDF-A6A6-F303-0A1C-217868AB07D4 and app
1093255277. Source preparation is in `3634960`; the supplied fragment has no Launch marker,
so the exact installed client revision/profile is not independently identified. The current
Sunshine version and time spent at error OK are not supplied. Earlier .134 version reports and
the other host's five-second pause must not be applied to this run.

Observed order:

1. Initial PAIRED connection and authenticated four-app list, then first selected resume.
2. `request headers sent; curl active before response` and `checkpoint during curl before response`.
   Session-owner stop request and main-thread `pending disconnect request result=0` occur next.
3. `response headers received`, then `transfer end result=0 checkpoint=1 response_headers=1`.
   Resume is accepted; the existing cancel check prevents RTSP and returns setup -7. The test
   observes STREAM_FAILED=-7 and CLOSED in DISCONNECTED state. The native cancellation error
   starts and terminates before reconnect is enqueued.
4. Same-host reconnect reaches PAIRED without a new PIN. Fresh applist count=4 and Apps rebuild
   before=4/after_delete=0/after_insert=4 precede restart of app 1093255277.
5. Encrypted RTSP succeeds, all startup stages complete, setup=0 and the test reports
   `PASS pending cancelled -> CLOSED -> reconnect -> stream started app=1093255277`.
   First video packet arrives after 400 ms; decoded frames continue through sampled frame 661
   (number 665). First audio packet arrives after 2700 ms and audio resync begins.

This verifies the targeted outgoing-header callback and Disconnect/HTTP-owner completion ordering
on one Vita, plus successful recovery through actual STREAM_STARTED, video decode and audio
packet receipt. Curl finishes normally before cancellation/host cleanup; no immediate abort or
server-side rollback is demonstrated. No callback trace bytes/keys appear in the supplied log.
The fragment ends during streaming. The maintainer subsequently confirms visible image,
audible sound and ordinary double-PS stop in reply to the combined playback/stop question.
This is user-reported confirmation, separate from the log evidence. Final STOPPED/release/
unmaps are not captured. The reply supplies no Sunshine version.
There is no basis to claim a leak-free lifecycle, repeatability, a specific error-OK delay or
resolution of the earlier RTSP tag failure. Memory
USER_RW is 33872 KiB both before the first Start and after paired reconnect, but that single
sample does not measure post-stream cleanup or allocator lifetime.

No production/test runtime code change is needed for this evidence. A16/A18/F5 remain partial
for HTTP errors/timeouts, RTSP/common-c startup cancellation, shutdown and repeated/concurrent
recovery. Next prepare a separate runtime trigger for cancellation inside RTSP/common-c startup.
The agent analyzed the maintainer log; no agent build/device run.

**Common-c startup test source preparation, 2026-10-21 (before maintainer run below):** the next
independent A16 gap is Disconnect after LiStartConnection has begun, while common-c owns partially initialized
resources. The minimal new mode MOONLIGHT_STARTUP_DISCONNECT_TEST=ON holds the existing
stageStarting(STAGE_RTSP_HANDSHAKE) callback: platform/name/audio initialization has succeeded,
RTSP handshake I/O has not started yet. The main-thread poll requests real Disconnect while
Launch is busy, then releases the one-shot bounded worker barrier. It uses a separate controller
because the earlier pending/HTTP tests require pre-common-c -7 and forbid any early STARTED.

Current source behavior/root cause of deferred cancellation: moonlight_stream_stop sets local
cancel/stop flags; run_stream_session checks them before LiStartConnection, but the call itself
never receives LiInterruptConnection. A successful handshake can therefore still emit STARTED
before returning, followed by owner LiStopConnection/STOPPED and host cleanup/CLOSED. This is
already noted in A16; no new runtime failure is asserted. The test must expose, not hide, this
late start. It requires correct host/app and STARTED → STOPPED → CLOSED event order, paired
reconnect, fresh Apps and an actual same-app recovery STREAM_STARTED/STREAMING. PASS explicitly
labels late_start=1 and means teardown/recovery only. Startup errors (including -7 if returned)
keep their real result, report FAIL and stop automatic actions; they cannot be promoted to
successful cancellation. Queued STARTED/STOPPED may be delivered after adapter state advances,
so event identity/order matters; CLOSED must still observe DISCONNECTED.

Reference comparison: vendored and current upstream common-c
[Connection.c](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Connection.c)
invoke the stage callback before performRtspHandshake, unwind failed setup through LiStopConnection
and document that interrupt is asynchronous: wait for LiStartConnection to return before reuse.
Moonlight Qt [Session::interrupt/deferred cleanup](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/session.cpp)
uses LiInterruptConnection with session cleanup serialization. A production interrupt follow-up
would need publication of safe common-c entry after its interrupt-flag reset and retention of the
single teardown owner/return-before-reuse invariant. Calling interrupt from the test alone would
change the path being measured. No production interrupt/endpoint/Sunshine workaround is added.

Regression risks: transient STARTED can open Stream before owner stop, and events may already
be queued while adapter state is DISCONNECTED. Normal frontend callbacks still own page/surface
cleanup; the test neither filters those events nor drives UI from the worker. A callback holds
no HTTP/stream/control mutex; failure/shutdown releases the barrier and a 60-second worker
safety deadline prevents indefinite test waiting. Test timeout does not prove backend cleanup.
Normal builds exclude the source/hooks. All five modes default OFF and are mutually exclusive;
build.sh resets cached selections before applying explicit arguments.

All 73 Python source checks pass, including the actual new controller/gate, stage filtering,
late-event ordering, busy admission/recovery, wrong/duplicate events, real failure preservation,
timeout/shutdown release, the guarded callback and all 32 five-mode Boolean combinations.
Atomics, scheduling/services and memory are mocked; no compilation/CMake configure, real
common-c/RTSP/TLS, PAF navigation, synchronization, device or leak validation was run. This
checkpoint covers partial startup at RTSP stage entry, not mid-handshake blocking I/O or prompt
cancellation latency. Build/Vita remain maintainer work; A16/A18/F5 stay partial. Next run the
[startup test procedure](../tests/runtime/README.md#common-c-startup-scenario)
and capture startup, owner stop/CLOSED, recovery playback and final cleanup separately.

**Common-c startup maintainer Vita result, 2026-10-21:** host 192.168.1.134, server UUID
3D7D0EDF-A6A6-F303-0A1C-217868AB07D4, app 1093255277. Source mode was prepared in `95eb968`;
the fragment has no Launch marker/current Sunshine version, so exact installed client revision/
profile and host version remain unspecified. Current checkout includes the maintainer's later
`c2040c9` asset commit; this does not identify the installed binary.

Observed sequence: accepted resume → LiStartConnection stages 1–3 → stage-4 checkpoint →
Disconnect=0 → successful RTSP and remaining startup → connectionStarted/setup=0 → immediate
session-owner stop. Late STREAM_STARTED is delivered while stop is underway and the ordinary
Stream page opens. Audio port 6 drain/release return 0; all common-c stop/cleanup stages finish,
owner result=0 → matching STOPPED → CLOSED/DISCONNECTED. Ordinary frontend handling restores
the infobar. Reconnect is enqueued before deferred framebuffer release; all three old unmaps
return 0 and `deferred frame buffers released` occurs before the new stream setup. This is the
intended separation of host cleanup from PAF surface lifetime, not proof that CLOSED waits for
all frontend resources.

Reconnect reaches PAIRED without PIN, authenticated applist count=4 and Apps rebuild
before=4/after_delete=0/after_insert=4. Same-app resume/encrypted RTSP/all startup stages succeed;
setup=0 and `PASS startup owner STOPPED -> CLOSED -> reconnect -> stream started app=1093255277
late_start=1` are logged. First video arrives after 300 ms. After frame 1/number 4, IDR wait,
consecutive-drop limit and IDR request, decode continues through sampled frame 301/number 424.
This demonstrates recovery after that burst, not sustained stream quality acceptance.

The first intentionally stopped session reports no received audio/video and decoded frames=0.
Its ENet interruption/control-failed:4 occurs during owner stop, followed by successful cleanup.
Local common-c Misc.c maps ConnectionInterrupted to EINTR; LiStopConnection sets that flag and
disables termination callbacks before teardown. These lines do not establish a failure of the
second session. The second session initializes audio but the fragment contains no received-audio
marker or final manual stop/release. The maintainer subsequently confirms visible image, audible
sound and ordinary double-PS stop (user report, separate from the log evidence).
USER_RW free is 33880 KiB initially, 27376 after first transport stop, 33336 after old framebuffer
release and 33600 at paired reconnect. The 280 KiB difference from the initial connection is a
single-cycle observation, not a leak diagnosis or a leak-free claim.

Record this targeted late-start owner cleanup/reconnect/resume/PASS as verified. The log also
confirms deferred cancellation reaches STARTED before stop; it does not implement/verify prompt
LiInterruptConnection integration, blocked-RTSP cancellation or failed-init rollback. A16/A18/F5
remain partial for those paths, timeouts, shutdown and repeats. Next prepare an isolated safe
startup-interrupt follow-up retaining the entry/reset/owner invariants above. Final recovery
resource-release logs remain unprovided. No runtime-code change or agent build/device run
accompanies this record.

**Safe startup interruption source preparation, 2026-10-21 (before maintainer run below):** the baseline
late_start=1 trace confirms the root cause: moonlight_stream_stop only sets local flags while
LiStartConnection continues. Blindly calling interrupt before the call also races common-c's
ConnectionInterrupted=false reset. The minimal production change stays in L2, with no vendored
edits. The first existing stage callback publishes an interruptible scope under s_stream_mutex,
after that reset, and replays a cancel already accepted. Stop under the same mutex issues one
LiInterruptConnection only inside that scope, before the started-state commit. The native
interrupt call is nonblocking; no stop/join runs under the mutex or on the requesting thread.

on_connection_started closes the interruptible scope and atomically arbitrates cancellation
against STARTED publication. A winning cancel skips Vita input startup and STREAM_STARTED.
The owner closes the scope again after LiStartConnection returns. If an interrupt was issued,
cancel remains set and STARTED never committed, it reports local -7 after cleanup: failed
setup has already unwound in common-c; a successful return instead requires one owner-side
LiStopConnection before -7. The raw result remains logged. This defines local cancel precedence,
not proof that every simultaneous network error was caused by the interrupt. With no admitted
interrupt (including validation failure before the first callback), the original error stays.
A stop after STARTED commits uses the unchanged active-session stop path. The reservation remains
held through terminal-event/API bookkeeping; deferred host cleanup waits for worker completion,
preventing interrupt or reuse of a newer session. Flags reset at each new owner reservation.

Related resource root cause: renderer cleanup defers mapped framebuffer release to PAF, but
Apps previously scheduled that release only on STOPPED. Suppressed STARTED/late-init failure
can own video buffers yet finish as STREAM_FAILED. Apps now schedules the same existing
main-thread, pool-generation/active-Stream-guarded release on FAILED as well. It does not free
GPU memory from the requesting thread or introduce a second transport owner. Native error UI
and modal completion remain unchanged; docs/UI.md records the lifecycle addition.

Reference comparison, rechecked 2026-10-21: the common-c
[public API](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Limelight.h)
requires return-before-reuse after asynchronous interrupt. Its
[Connection.c](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Connection.c)
resets interruption before stageStarting(PLATFORM_INIT), performs failed-init unwind itself and
can still call connectionStarted on a successful path; the callback cannot return an error.
Moonlight Qt [Session::interrupt/deferred cleanup](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/session.cpp)
uses the native interrupt and serialized cleanup. The Vita adapter adds the local entry/reset
and event arbitration needed by its own owner model; it does not copy Qt/SDL threading or patch
Sunshine. HTTP still completes under its original timeout; remote app/pending-session rollback
and host-wide quit remain outside local cancellation.

The existing startup checkpoint is unchanged and now validates -7 → CLOSED → ordinary error
OK → paired reconnect/fresh Apps/same-app STREAM_STARTED with PASS late_start=0. A first STARTED
or STOPPED is a failure; the old late_start=1 Vita success remains historical only. The test
never calls interrupt itself. Earlier Sunshine pending-session encrypted RTSP recovery failure
can still occur and must be recorded separately; no delay/retry/key reuse/downgrade is added.

All 82 non-build Python checks pass, including actual production helper interpretation and
before/reset/after-entry cancel orderings, duplicate requests, success/cancel arbitration,
owner stop exactly once for cancelled success, no double-stop on failed-init unwind, preserved
uncancelled/pre-callback errors and owner-release/host-cleanup order. An actual Apps failure/
deferred-release check covers error scheduling, the existing delay and stale-generation/active-
Stream guards. Startup-controller checks require user-paced error OK and reject early/duplicate/
wrong-target events. Mutexes,
interrupts, native input/events, atomics and scheduling are mocked serially: no C++ type/ABI,
real concurrency/common-c/RTSP/TLS/PAF, allocator or device proof. bash syntax/diff checks pass;
no agent build/device run. Regression checks still needed: ordinary startup/active stop, stage-4
partial unwind, late-stage/success-race buffers, blocked-I/O latency, repeated recovery and
shutdown. Native interruption is cooperative and does not guarantee immediate cancellation of
every blocking socket/platform call. A16/A18/F5 remain partial until maintainer evidence exists.
Next run the [updated startup test](../tests/runtime/README.md#common-c-startup-scenario).

**Interrupted startup maintainer Vita result, 2026-10-21:** host 192.168.1.134, server UUID
3D7D0EDF-A6A6-F303-0A1C-217868AB07D4, app 1093255277. The new strict controller and production
cancel/unwind markers identify the follow-up behavior introduced in 499a241. No Launch marker,
current Sunshine version or measured time at error OK is supplied; do not assign an exact
installed revision/profile or reuse earlier host-version/timing reports.

Observed sequence:

1. Accepted resume reaches the checkpoint at RTSP stage 4. Disconnect=0 precedes RTSP OPTIONS/
   stage failure -1, audio-stream/platform cleanup and LiStartConnection return -1. The production
   log records `common-c startup cancelled result=-1; owner cleanup returned`, then setup -7.
   This matches the native cooperative interrupt at the stage-entry transaction check; the raw
   OPTIONS failure here is part of expected cancellation, not evidence of failed recovery.
2. Matching STREAM_FAILED=-7 is delivered before CLOSED/DISCONNECTED. The ordinary native error
   starts and terminates before reconnect. There is no initial connectionStarted/input startup,
   video decoder allocation or StreamPage in the supplied cancellation segment. Therefore this
   does not exercise release of buffers allocated during a later interrupted stage.
3. Reconnect reaches PAIRED without PIN; authenticated applist count=4 and Apps rebuild
   before=4/after_delete=0/after_insert=4 precede same-app resume. Encrypted RTSP/all startup
   stages succeed, setup=0 and PASS late_start=0 are logged. First video arrives at 400 ms;
   decode reaches sampled frame 181/number 184 and the final decoded count is 208.
4. Double PS requests normal stop. Owner result=0 follows full common-c cleanup, audio port 6
   drain/release both return 0, the infobar is restored, and three video unmaps return 0 before
   `deferred frame buffers released`. This verifies final normal teardown separately from the
   first partial-init unwind.

Recovery audio initializes, but stop explicitly reports `No audio traffic was ever received
from the host!`; no received-audio marker appears. The maintainer subsequently answers that
sound was audible on Vita for this run. Retain both observations: playback is user-reported,
while this trace does not establish audio packet receipt. The discrepancy is unresolved.
Vendored AudioStream.c sets receivedDataFromPeer/logs on first packet and reads that flag for
the stop warning before joining its receiver; no correlated capture identifies why this warning
and the audible report disagree. It is a short session (208 decoded frames, no absolute duration
supplied). No audio code change or diagnosed regression follows from this evidence alone.
ENet interrupted/control-failed:4 occurs during final owner stop and cleanup still succeeds. USER_RW free returns to 33880 KiB
at paired reconnect (same as initial), then 33600 after final framebuffer release; one cycle
neither proves leak freedom nor diagnoses the 280 KiB difference.

Record stage-entry interruption, partial-init unwind, error acknowledgement, paired recovery/
video/PASS late_start=0 and final normal resource release as verified on this Vita. Do not
extend it to cancel-before-entry replay, blocked RTSP I/O latency, late-stage/cancelled-success
buffers, shutdown, repeats or log-verified audio receipt. Earlier immediate-recovery/tag-failure
runs remain separate; this success does not establish a universal safe delay or fix Sunshine's pending slot.
A16/A18/F5 remain partial. Next target cancellation after video allocation to exercise FAILED/
deferred-buffer release. No runtime-code change or agent
build/device run accompanies this evidence record.

**After-video-allocation startup checkpoint source preparation, 2026-10-21:** the
stage-4 result cannot validate STREAM_FAILED cleanup of decoder/framebuffer resources because
it cancels before video setup. The minimal next step is a selectable checkpoint in the existing
startup test, with no new endpoint or production cancellation change. Use
`MOONLIGHT_STARTUP_DISCONNECT_TEST=ON` plus `MOONLIGHT_STARTUP_DISCONNECT_STAGE=10` (default 4).

Reference comparison: both local and current upstream
[Connection.c](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Connection.c)
invoke stageStarting(AUDIO_STREAM_START=10) after successful video startup/stageComplete(9),
before audio startup/connectionStarted. [VideoStream.c](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/VideoStream.c)
performs renderer setup before starting video threads. The worker captures the actual Vita
three-slot pool generation at this boundary and pauses until the main thread makes the real
Disconnect request. Missing/partial buffers or a zero generation fail the test. Native cleanup
can follow either a failed common-c return or a cancelled success stopped by the owner; local
-7 must precede CLOSED, without an initial frontend STARTED/STOPPED.

After ordinary error OK/CLOSED, the controller waits for the captured generation's metadata
to clear before reconnect. A changed generation or 60-second post-OK step timeout ends automation.
No main-thread sleep is added. The test-only observer reads slots on the owner at the checkpoint
or on the main thread after completed host teardown. Count zero proves only cleared bookkeeping;
runtime acceptance also requires all three old unmap return codes 0 and the deferred-release
marker before reconnect/new allocation. The existing release path does not separately log kernel
free return values. Then require PAIRED without PIN, fresh Apps and actual same-app STARTED/
PASS late_start=0 stage=10, image/sound confirmation and final manual stop/resource cleanup.

Regression scope: only the opt-in controller, test-only renderer observer and build selector
change; no production allocation/free/stop behavior, third-party code, Sunshine workaround,
retry/delay, credentials or UI modification. All five modes remain OFF/exclusive, and build.sh
resets stage 4 before explicit overrides. All 88 Python source checks pass, including 512 mocked
slot-presence combinations, release ordering and rejection guards. No configure/build, compiler/
ABI, actual atomics/concurrency, GPU lifetime or device proof from those checks. A16/A18/F5
remain partial; the subsequent maintainer evidence below covers the targeted late-init case.
Exact command and log requirements are in
[the runtime procedure](../tests/runtime/README.md#after-video-allocation-checkpoint).

**After-video-allocation maintainer Vita result, 2026-10-21:** host 192.168.1.134/server UUID
3D7D0EDF-A6A6-F303-0A1C-217868AB07D4, app 1093255277; maintainer-reported Sunshine
2025.924.154138. The checkpoint/PASS stage=10 markers identify the test introduced in 2c11d3b,
but the attachment has no Launch marker/exact installed revision. Error-OK timing and firmware
are unspecified; do not infer a safe pause or transfer the timing from another run.

Observed lifecycle:

1. First resume accepts an encrypted RTSP URL; video setup maps three buffers before stage 10
   reports buffers=3/generation=1. Disconnect=0 requests real startup interruption. Native audio/
   input startup still completes, then `connection ready after cancel; owner cleanup pending`
   suppresses frontend input/STARTED. The owner stops input/audio/video/control and cleans all
   common-c stages. Audio drain/release return 0; video decoded frames=0. Raw common-c success
   is logged as `common-c startup cancelled result=0; owner cleanup returned`, then local setup
   -7. Thus this tests the cancelled-success arbitration branch, unlike stage 4's failed-init unwind.
2. Expected STREAM_FAILED=-7/error 0xFFFFFFF9 precedes matching CLOSED. All three old-pool unmaps
   return 0 and deferred release completes before the native error dialog terminates. The
   generation=1 clear marker precedes reconnect and the next allocation. No initial frontend
   STARTED/STREAM_STOPPED, Vita input start or Stream page is present. The old buffer addresses
   are reused only during recovery setup, after that release. Existing code does not separately
   log native kernel free return values; successful GXM unmaps and metadata clearance are observed.
3. Reconnect is PAIRED without PIN; fresh applist count=4 and before=4/after_delete=0/after_insert=4
   precede same-app resume/encrypted RTSP/setup=0/PASS late_start=0 stage=10. Recovery receives
   first video at 500 ms and first audio at 3000 ms, and final decoded count is 445. The maintainer
   explicitly confirms visible image and audible sound for this run.
4. Final double PS requests the ordinary owner stop. Audio drain/release return 0; owner stop
   result=0, infobar restore, three successful unmaps and deferred-release completion are logged.

The first cancelled attempt's no-audio/video warnings belong to its immediate stop; the recovery
explicitly receives both. This does not explain the separate earlier run's audio discrepancy.
Recovery also logs an OOS/speculative-RFI exit, IDR wait/drop limit/request, then resumed decoding
and connection status 1 → 0. This is recovery evidence, not error-free playback or a diagnosed
network/decoder issue. USER_RW free is 33872 KiB initially, 33868 at paired reconnect and 33592
after final deferred release. One cycle/different UI states neither prove leak freedom nor diagnose
a leak. Host-side workers/process/input/display cleanup are not independently traced.

Record the targeted late cancelled-success owner stop, FAILED deferred-buffer release before
reconnect, paired recovery/playback and final teardown as verified on this Vita. Other admission/
callback races, blocked-I/O latency, earlier-entry replay, shutdown, repeats and the full T7/T8
matrix remain open; A16/A18/F5 stay partial. The older immediate-recovery RTSP tag failure is
still valid evidence; this success does not fix Sunshine or establish universal retry timing.
No runtime change or agent build/device run accompanies this record. Next check repeated
ordinary start/stop cycles with test modes OFF.

**Three ordinary start/stop cycles — maintainer Vita result, 2026-10-21:** the Launch marker
identifies `profile=system system_app=ON commit=98f0d44194af`; host 192.168.1.134/server UUID
3D7D0EDF-A6A6-F303-0A1C-217868AB07D4, app 1093255277. Unlike the preceding startup tests,
no test-controller checkpoint/automatic disconnect appears. One initial paired connection and
applist count=4 precede three ordinary Starts using has_host=1/state=2. Each refreshes serverinfo,
resumes the same app over an accepted encrypted RTSP URL, completes setup=0 and opens Stream.
First video packet delays are 400/400/300 ms; audio delays are 1700/0/0 ms. Final decoded counts
are 1833/1856/1840 (5529 total). Each stream logs OOS/speculative-RFI/fast-audio-recovery
transitions with continued decoding; this is not an error-free quality or sustained FPS result.
Audible/visible playback and the exact current Sunshine release require maintainer confirmation;
the previous run's reported release is not automatically assigned to this run.

Each deliberate double-PS stop requests the session owner, stops input/audio/video/control and
cleans all common-c stages. Audio port 6 drain/release return 0, owner stop returns 0, infobar
restore succeeds, all three GXM unmaps return 0 and deferred release completes before the next
Start (or the end of the third cycle). ENet interrupted/control-failed:4 appears only during
requested stop; successful teardown follows. No setup failure, unexpected termination, no-audio/
video warning, resource-retention or allocation failure is logged. Successful native kernel
free return values are not separately logged. There is no host DisconnectHost/CLOSED/reconnect
between Starts and no application shutdown trace.

Memory observations, in equivalent post-release snapshots: USER_RW free is 33596 KiB after
every cycle; after-stop/before deferred release it is 27412 KiB in all three. Stream-ready
USER_RW is 10828 KiB each time. Initial paired idle is 33876 KiB, so the first-cycle difference
of 280 KiB does not continue to grow at this resolution. However, the post-release newlib arena
is 196608 bytes with used=98792/99096/99256 (increments 304 and 160, +464 bytes over two repeats).
These are different measurements: kernel free memory is not newlib live allocation accounting.
Three samples establish neither leak freedom nor a leak/root cause for the small newlib rise;
longer controlled runs or allocation attribution are separate work, not a reason to change
working resource cleanup speculatively.

Record three sequential normal start/stop/resource-reuse cycles as verified on this Vita.
This binary predates ee485a2 and provides no validation of that later Main empty-label fix.
Shutdown during an active stream, repeated failure/host reconnect, admission/callback races,
host-side cleanup and broader T7/T8 acceptance remain open. No runtime-code change or agent
build/device run accompanies this evidence record. Next inspect/prepare active-stream
application-shutdown validation.

**Snapshot-pressure event delivery source correction, 2026-10-21 — build/Vita pending:**
QueueEvent's common 16-slot ring allowed repeated HOSTS_CHANGED notifications to crowd out
pairing/stream/closure. Logging the drop and retaining only Connect/Apps job results did not
restore those events. The correction extracts a small allocation-free queue into app/event_queue.c:

- Only HOSTS_CHANGED and SETTINGS_CHANGED are snapshot invalidations: readers fetch complete
  state. Each owns one latest-value slot outside the unchanged 16-slot ordered FIFO. Scan
  start/finish/failure, pairing, stream and connection events are never coalesced or deduplicated.
- Sequence order merges the latest snapshots with ordered events. Peek moves the head to an
  owned pending slot before releasing the PAF mutex. Repeated peek while PrepareCompletion
  returns false sees the same payload; producers can update snapshots/fill the ring without
  changing it. Acknowledge precedes page dispatch so a callback's new notification cannot be
  erased by acknowledgement of the old event. Reset clears pending and queued work on init/shutdown.
- Address strings are bounded/copied/rebound at both storage moves; PIN/server-error arrays
  remain value-owned. The dispatcher also copies the address to its delivery stack before unlock.
  Existing Connect/Apps fallback and native-dialog/main-thread delivery remain intact. No
  protocol, service admission, page navigation, CXML, credentials, stream cleanup or test mode changes.

The bound is explicit: 16 queued ordered events, two snapshots and one extracted pending head.
More than 16 simultaneous queued ordered events is rejected and logged without overwriting
accepted work; producers never wait while holding backend/main-thread resources. This source
fix protects the sequential workflow from discovery/settings pressure; it does not promise
unbounded delivery under a critical-only storm or fix wider callback/session/shutdown races.
No additional per-event success/heartbeat logs or dynamic allocations are introduced.

Reference comparison: [BHBB jobs/UI synchronization](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/master/src/pages/app_browser.cpp)
locks PAF UI access; [Moonlight Qt termination marshalling](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/session.cpp)
sends a main-loop event. The retained queue is project-specific; neither reference supplies
this queue or justifies doing PAF work from the streaming callback. Protocol sources remain unchanged.

All 100 non-build Python checks pass, including 12 new checks interpreting the actual queue C
and ProcessEventTask source with modeled C values/pointers, mutexes and serial interleavings.
Coverage includes 400 snapshot updates around lifecycle events, failure/scan ordering, pending
completion stability, address/PIN/error copying, no replay/reset/wrap, callback-produced updates,
Connect fallback and explicit ordered-only overload. These are not C/C++ compilation, ABI,
real concurrency, native-dialog or Vita evidence. Maintainer smoke: discovery → pairing success/
error → Apps → launch → double-PS stop → restart; confirm normal list refresh, WAIT/PIN completion
and resource release. No new hardware test trigger is required. Next independent implementation
is honest host persistence error propagation; F5's broader runtime/race matrix remains open.

**Regression risks / tests:** queue saturation, old-session events affecting a new session, host
switch and page close during jobs; T7/T8. This is consistent with the existing repository analysis;
it does not justify a broad architecture rewrite during this audit.

### F6 — Wrong-PIN retry on Sunshine 2025.924.154138 — accepted noncritical limitation

The Vita rejects the first incorrect PIN with semantic error -1001. After generating a
new PIN and submitting it in Sunshine, the next HTTP getservercert request receives no
response bytes, becomes network error -1002, and Pairing returns to Main. Successful
retry is not verified and must not be reported as supported for this tested sequence.

Source review of [Sunshine v2025.924.154138 pairing](https://github.com/LizardByte/Sunshine/blob/v2025.924.154138/src/nvhttp.cpp)
found reuse of an existing pairing session for the same client uniqueid after the
client rejected the PIN. This is consistent with the failure, but the supplied logs
do not directly prove the exact server-side failure path.

On 2026-10-20 the maintainer explicitly accepted this as noncritical and excluded
Sunshine fixes and client workarounds. The uncommitted host patch/materials were
removed; no runtime code was changed for F6 at that stage. The later F7 client retry-loop
fix is separate from this server compatibility limitation. Preserve client identity,
authentication and error classification. Continue independent client API work.

### F7 — Pairing error dismissal starts another attempt — Search failure flow verified on one Vita

**Root cause:** Pairing::DialogPollTask unconditionally called RetryPairing after every
non-network error dialog finished. RetryPairing generated another PIN and enqueued Pair.
Thus a protocol rejection (including duplicate pairing-session status) could repeatedly
produce another request, another dialog and another PIN. The new runtime log demonstrates
that sequence. It does not prove unsolicited dialog completion without user input.

**Reference:** [Moonlight Qt NvPairingManager::pair](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/backend/nvpairingmanager.cpp)
returns a terminal PIN_WRONG/FAILED result for a failed attempt. Its separate legacy
/unpair cleanup is not applicable to Sunshine and is not copied here.

**Minimal change:** every failed attempt ends after its existing native OK dialog and
returns through the Base stack to the parent PC page. Remove RetryPairing and its obsolete
error-result member; change the incorrect-PIN message to describe explicit PC selection.
The constructor still starts a user-selected attempt; successful pairing still opens Apps.
No new endpoint, timer, server patch, authentication change or client identity change.
This addresses client error navigation and does not promise immediate server retry readiness.

**Regression risks / checks:** non-build diff/call-path review and existing Python checks;
no application or resource build. On Vita, incorrect PIN and protocol/network/security
errors should each produce one dialog, then Search/Main, with no new request/PIN while
idle. Select the PC again to verify explicit re-entry; record any retained host session
as a server response, without restarting a client loop. Check normal correct-PIN → Apps
and Back behavior. The follow-up above verifies the incorrect-PIN and duplicate-session
Search returns and explicit re-entry on one Vita; Main/manual paths, other error types
and correct-PIN recovery still need maintainer tests. No build was run by the agent.

### Pairing protocol-error details — source follow-up, 2026-10-20

**Trigger/root cause:** the maintainer compared Moonlight Qt's duplicate-session dialog
with the Vita's generic protocol error. Legacy xml_status_ok read the reason into a
global error string, but discarded the numeric status; MoonlightEvent conveyed only
semantic -1003. The UI therefore could not distinguish the server's 409 response from
other protocol failures. Reading a mutable legacy buffer directly from the UI would
also violate the queued service/backend boundary.

**Reference:** [Moonlight Qt NvHTTP::verifyResponseStatus](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/backend/nvhttp.cpp)
extracts XML status_code/status_message and returns them in GfeHttpResponseException.
Use that diagnostic contract, without copying the legacy GeForce Experience product
name into a dialog for a Sunshine PC.

**Minimal source change:** legacy pairing returns bounded status/message through a
caller-owned LegacyGameStreamPairingError. Valid non-200 XML responses fill the details;
missing/malformed/success responses clear them. XML attribute order does not determine
whether a message is captured, and truncation preserves UTF-8 code-point boundaries.
The API copies details into inline MoonlightEvent fields before dispatch. The existing
adapter/queue copies those fields by value, so the UI never holds a legacy-buffer
pointer. Settings events are zero-initialized too, to keep the extended event payload
defined for all producers. The 16-entry queue grows by about 4 KiB.

Protocol dialogs show `The PC returned a pairing error:` plus the host reason and
`(Error <code>)`. Status without a reason uses a generic reason plus the code; absent
status keeps the existing generic dialog. Incorrect-PIN/network/security/internal
classification and messages remain separate. No retry, endpoint or pairing-trust
change is added. The F7 one-dialog terminal flow remains unchanged.

**Checks/limits:** existing six non-build Python checks pass, plus an ad hoc pycparser
C grammar check of the changed XML status helpers. Source/diff review covers all five
pairing phases, initialization, bounded strings and event-copy lifetime. These checks
do not execute the status parser or PAF. No build or real PAF/HTTP test was run by the agent. Verify 409 text/code, a different
server error, missing message, wrong PIN, correct PIN and OK → Search/Main on Vita.
Do not mark the new diagnostic text hardware-verified from the older generic-dialog log.

**Runtime follow-up, 2026-10-20:** a new maintainer log contains two explicit Search
selections. Both connect successfully, then getservercert immediately responds with
semantic -1003, server_status=409 and "A pairing session with this uniqueid already
exists"; neither reaches clientchallenge or PIN verification. This confirms extraction
of the server code/reason at the API boundary on Vita. It does not identify which prior
request owns that host session or its remaining lifetime: no server log/timestamps are
provided. The immediate failure is the host's explicit duplicate-session rejection,
not evidence that the new display code caused a handshake failure. The maintainer
subsequently confirms the Vita dialog displays the duplicate-session text with Error 409.
That verifies this specific diagnostic on one Vita; other codes, long/Unicode messages
and the broader display/error matrix remain open. No runtime change is justified
by these two refusals; retry after the host finishes its prior attempt, then verify
correct-PIN pairing/Apps separately.

**Maintainer follow-up, 2026-10-20:** after the requested retry once the host's prior
attempt finishes, the maintainer reports "ну теперь вроде всё ок" (qualified user
confirmation that the discussed flow now appears OK). Record this as a user-reported
recovery smoke result, without a new runtime trace. Final PIN/HTTPS phases, Apps
retrieval, repeated recovery and the exact second-host version are not independently
confirmed by this message. No further runtime change is warranted for the reported
flow; continue the independent app-list presentation task. F6 remains an accepted
version-specific limitation and the broader T3/T9 matrix stays open.

### Apps presentation beyond eight — separate source follow-up, 2026-10-20

**Root cause:** page Apps requested/clamped to kMaxApps=8 and owned both app and callback
arrays of eight, despite the API cache already holding 64. Raising the parser limit
alone could not expose entries 9–64. The PAF resource has no eight-cell restriction.

**Reference:** the locally inspected BetterHomebrewBrowser `9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5`
[factory and EntryCB](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/src/pages/app_browser.cpp)
use page-owned list data and a widget name containing the entry's identity, with the
page as callback userdata. Its ClearList uses the full cell count. These are PAF
ownership references; no streaming/protocol code is moved into Apps.

**Minimal change:** Apps owns a paf::vector sized to APPLICATIONS_READY's stored count.
A shared MOONLIGHT_MAX_APPLICATIONS=64 expresses the unchanged backend cache bound.
Old cells are deleted before data replacement; invalid counts/cache-copy errors leave
an empty list with an error, while a valid zero count shows the empty state without
issuing another refresh. Rows carry the real application ID and resolve it in the page's
current snapshot on selection; no callback retains an address in resizable storage.
The existing ListView/template handle scrolling. One console-only population diagnostic
reports count and cells before/delete/insert. Main/Search host limits are unchanged.

**Risks/validation:** check 0/1/9/64 apps, 65 configured apps (the API still stores only
64), select entries 9/64 and compare the launch ID, reopen with a shorter/empty list,
switch between hosts, and check normal stream return/Back. The existing six non-build
checks pass and source review covers ready-count delivery, SDK vector/name APIs and
callback/data lifetime. They do not execute PAF. No build or new hardware test was run
by the agent. F5 shared-cache/event races remain outside this change.

**Vita follow-up, 2026-10-20:** the maintainer reports the requested >8-app check OK
and supplies:

```text
[GameStream] applist count=10 total=10
[Apps] ShowApps count=10 cells before=0 after_delete=0 after_insert=10
```

This verifies copying/populating all ten apps on one Vita, beyond the former eight-row
limit. The requested scrolling/selection check is user-reported OK; the fragment has
no selected application ID or launch trace. It starts with an empty ListView, so it
does not exercise deleting a nonempty list. The 64/65 boundary, empty/smaller-list
rebuild and two-host cache isolation remain pending. No agent build/hardware test.

**Two-PC follow-up, 2026-10-20:** the maintainer confirms the requested sequence of
switching between two PCs with different app lists and returning to the first: the
visible lists correspond to the selected PC. This is user-reported normal-flow success
without a new runtime log or a separately specified package/version. It covers basic
visible list isolation; it does not prove concurrent cache/job safety, full backend
DisconnectHost API coverage or all F5 races. Empty-list behavior, nonempty in-page
rebuild, exact launch-ID checks and the 64/65 boundary remain pending.

**Empty-list follow-up, 2026-10-20:** the maintainer confirms the requested empty-list
scenario: Apps shows `No applications available.`, without repeated requests or a hang.
This is a user-reported Vita result, without a new log or raw XML body; it does not
validate malformed/error responses or every refresh race. The 64/65 boundary, nonempty
in-page rebuild and exact launch-ID tracing remain pending. No runtime change is needed.

**Scope decision, 2026-10-20:** the maintainer explicitly defers the 64/65-app boundary
test. Keep it unverified; do not run it or treat it as a blocker for the completed basic
Apps improvement. The backend bound remains 64. Other outstanding tests keep their own
status; this decision is not acceptance of untested protocol/error or concurrency paths.

### F8 — Abandoned pairing holds the connection queue — active-wait cancellation/reconnect verified on one Vita

**New runtime evidence (2026-10-20):** on the older Sunshine host the maintainer reports
incorrect PIN → return to Main → immediate selection of that PC → apparent full app
freeze. The Vita system still allows minimizing the app. Initially only connection
`enqueue ... result=0` was available, without ConnectJob::Run. A later fragment shows
`pairing getservercert failed ... Timeout was reached`, semantic -1002/status 0, then
ConnectJob::Run immediately starts and finishes successfully, followed by another PIN
page/getservercert. Thus the queue was occupied by a pending pairing request, rather
than permanently deadlocked. The fragments do not identify how that particular pending
attempt outlived its page; the complete first-attempt trace and installed revision are
not available. Do not attribute an incorrect-PIN crypto failure to this second request.

**Source cause:** PairingService and ConnectionService use the default PAF job queue.
PairCurrentHost synchronously waits for the worker's getservercert request (up to 300 s),
and destroying Pairing did not cancel its job or transfer. Connect accepted another
job and opened the modal WAIT dialog while that earlier operation could still occupy
the queue. The newer-host single-dialog result F7 did not cover this abandoned-request
case. This client ownership problem is separate from accepted F6 server compatibility.

**Reference/minimal implementation:** use the documented libcurl
[CURLOPT_XFERINFOFUNCTION](https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html)
abort mechanism, with NOPROGRESS=0 for a cancellable transfer. Each PairingService job
owns a distinct monotonic atomic token, retained by the PAF queue during Run/Finish.
Page destruction, replacement pairing, application shutdown and a new Connect request
signal cancellation without taking the HTTP mutex or joining a worker on the UI thread.
Queued cancelled jobs skip the backend; active pairing HTTP phases and the final status
refresh check the token. Callback/data are detached under the HTTP mutex before a
transfer returns. Cancellation returns a distinct local result and suppresses obsolete
completion/error events. The existing final-HTTPS failure path rolls back temporary
server trust; cancellation after already completed host pairing cannot undo it.

The 300 s allowance remains for an attempt whose page is still open. No new queue,
shortened PIN-entry timeout, Sunshine patch, identity rotation, /cancel or /unpair is
added. Cancellation stops local I/O; it does not promise to erase a host pairing session
or prevent a later genuine 409. Curl progress scheduling (and DNS/connect behavior)
limits cancellation latency; do not promise instantaneous abort or a fixed duration.
Console-only job begin/return/Finish/release and cancel markers support the next test.
The initially prepared main-thread heartbeat was removed after the timeout log clarified
the queue wait; no recurring heartbeat is added.

**Checks/remaining:** all six non-build Python checks pass; the C grammar check now
includes the cancellation token, progress callback and HTTP helper. This is neither
compilation/type checking nor curl execution. Source review covers job/token lifetime,
callbacks, all pairing requests, error-event suppression and credential rollback. No agent build or hardware verification of the change. On Vita:
open Pairing and immediately Back → reselect PC; repeat after incorrect PIN on the
older host; cancel before a queued job starts; check cancel requested → local cancelled
result → ConnectJob::Run without the multi-minute timeout. Verify one 409/error dialog
if Sunshine retains its session, normal correct-PIN → Apps/Back, page close during
later pairing phases, app shutdown and repeated/two-host cycles. Continue checking F5
for broader queue/event/cache state races; this does not establish universal thread safety.

**Vita follow-up, 2026-10-20:** the supplied launch line identifies system profile,
commit `e7e7166ae5db`. For the previously identified older Sunshine host, the first
active getservercert wait receives cancel requested, returns legacy -11 (Pairing
cancelled locally), logs no completion event and semantic result -1006, then Finish
and destruction. A subsequent explicit connection job runs, returns 0, finishes and
is destroyed before another Pairing page opens. No preceding 300 s timeout is present
in this sequence. There are no timestamps, so exact cancellation/reconnect latency
is not measured. This verifies local active-wait cancellation, job release and
successful reconnect on this Vita/profile; it does not establish every race/path.

The next attempt reaches serverchallengeresp, rejects the incorrect PIN (-1001),
returns from Run and finishes. Closing its error releases the already-finished job;
the later cancel requested marker there is not a second network attempt. Another
explicit Connect also completes, followed by getservercert returning an empty HTTP
response (-1002, status 0), then Run return, Finish and destruction. That is the
already accepted old-host compatibility failure F6, not recurrence of queue starvation.
No new runtime change is justified from this trace. Successful correct-PIN pairing
through final HTTPS/Apps on this build, cancellation before Run, later-phase rollback,
shutdown, repeated/two-host cycles and normal-profile behavior remain pending.
The agent did not build or run on hardware.

**Second-host follow-up, 2026-10-20:** the maintainer selects the newer host, leaves
Pairing and selects it again. Its log shows the first getservercert being locally
cancelled (-11/-1006), no completion event, Run return, Finish and destruction. The
next Connect completes with result 0 and is released. Its new pairing request receives
a real server_status=409 / duplicate uniqueid rejection and also returns/finishes/releases.
Thus local cancellation/queue release also works in this second-host sequence; it does
not clear Sunshine's pending pairing session. There is no new launch/version line in
this fragment. No host cleanup endpoint or client workaround is added for the refusal.

Separately the maintainer explicitly confirms the ordinary correct-PIN flow succeeds
and opens Apps. This is user-reported regression success for the cancellation work;
the attached log contains cancellation/409, not the successful PIN/HTTPS/Apps phases.
It does not prove immediate successful re-pair after cancel or a change to F6. Normal
binary profile (distinct from an ordinary usage scenario), pre-Run cancellation,
later-phase rollback, shutdown, stress and alternating-host cases remain pending.
No runtime code change is needed for this result. Continue Apps >8-row validation.

**Normal-pairing follow-up, 2026-10-20:** the same new fragment shows PairingService
Run returned result=0, "Paired. Loading applications...", cancel requested, Finish and
job destruction, followed by the ten-app result above. This verifies normal pairing
completion, job release and Apps retrieval with the cancellation path present. The
page destructor's cancellation signal after Run already succeeded is not a failed
pairing result or a new handshake. No launch revision/profile line or phase-by-phase
TLS trace is included. Queued/later-phase cancellation, rollback, shutdown and repeated
or cross-profile validation remain open; no runtime change is needed from this result.

## 5. Validation catalogue (mixed evidence; remaining cases tracked separately)

T1–T10 describe the complete validation space, not ten wholly untested tasks or a requirement
to enumerate every fault injection before the next code change. Dated records above identify
the cases already verified; §6 separates outstanding implementation from deferred tests.

For every run record client source/package revision, Vita firmware/device, deployed Sunshine version
and configuration (ports, encryption policy, display setup, relevant disconnect/controller options),
selected app ID, Vita event order and Sunshine logs. Keep debug output in psp2shell; do not introduce
file-based stream logs or per-frame diagnostics.

- **T1 Discovery:** empty/one/multiple LAN hosts, >8 visible entries, repeat scan after natural
  completion, host disappears, nondefault HTTP port and SRV/A arrival order. Check no saved record
  is created just by discovery. Current UI limits must be recorded as failures/limits, not bypassed.
- **T2 Server info/list auth:** paired/unpaired, host offline, wrong/custom HTTP/HTTPS port,
  HTTPS denied but HTTP reachable, absent optional/required fields, two hosts and valid empty applist.
- **T3 Pairing:** correct and incorrect PIN, delayed/refused operator approval, expiry and retry,
  network loss at each phase, malformed/invalid proof, crypto allocation/key errors, final HTTPS
  failure, metadata write failure. Pairing success must remain separate from persistence failure.
- **T4 Parser/UI:** 0, 1, 9, 64 and 65 apps; escaped `&`, `<`, Unicode titles; invalid/non-numeric ID,
  missing title/ID, nested/incomplete App, malformed XML, truncated body and XML error with HTTP 200.
  Separate parser tests from Vita item selection/list-count checks; cache must not cross hosts.
- **T5 Launch/resume:** idle launch, same-app resume, selection B while A runs, host app exits/changes
  after connect, capture error, invalid app ID, XML status/session mismatch, slow startup, supported
  and unsupported display modes. Confirm actual host process ID and negotiated dimensions.
- **T6 RTSP/start:** encrypted and legacy URL behavior, nondefault RTSP/transport ports, rejected
  OPTIONS/DESCRIBE/SETUP/ANNOUNCE/PLAY, timeout/network loss at each stage, renderer/audio failure,
  duplicate start; verify cleanup and no falsely successful stream event.
- **T7 Stop/disconnect:** explicit stop after setup worker returned; repeated stop/disconnect;
  disconnect idle, during HTTP launch, between launch and RTSP, inside LiStartConnection, and active
  streaming; stop worker creation failure. A backend test trigger is required where UI has no caller.
  Match STARTED/FAILED/STOPPED/CLOSED ordering, worker completion, neutral input and surface lifetime.
  On Sunshine distinguish pending-session expiry, stream worker exit, process retention and display reset.
- **T8 Reconnect:** immediate restart and repeated sessions after manual stop, explicit disconnect,
  host graceful termination and network loss; allow network recovery, then reconnect. Confirm prior
  common-c teardown completed and no stale event/input/framebuffer affects the next session. Include
  Quick Actions and deliberate PS gestures as regressions; do not reopen proven input paths without data.
- **T9 Trust/optional remote actions:** server certificate replacement, revoked/disabled client and
  corrupted local credentials; retry/re-pair behavior. Test old records without server.pem, new
  pin persistence across process restart, malformed/unreadable/missing pins, final challenge failure,
  pin-write failure and two distinct hosts. Confirm mismatch cannot fall back to HTTP or overwrite trust. Remote Quit/unpair tests require an explicitly
  requested feature; ordinary stop must leave other clients/apps unaffected by a `/cancel` request.
- **T10 Compatibility:** record each actually tested Sunshine release/configuration; exercise
  encryption policy and IPv4 custom ports. Test older GFE only if support is claimed. Do not infer
  compatibility from `unsupported=true`, fallback constants or successful desktop compilation.

## 6. Completion and next work

### 6.1 Current conclusion — 2026-10-21

Reconciled with local master edf0b58 (including ee485a2), the existing source inventory and
maintainer evidence. The original inventory is complete; the normal IPv4/H.264/stereo client
workflow has effective implementations and scoped Vita verification. No required ordinary-flow
HTTP endpoint was found missing. This is not a full compatibility/security/release certificate.
A failed/unrun edge test does not by itself mean an endpoint or implementation is absent.
The detailed A01–A24 entries and dated F1–F8 records remain the evidence, not this summary alone.

**Source status and runtime evidence are separate.** A08's parser and A09/A10's normal launch/
selected-app resume are implemented; their headings now say so while retaining unrun fixture,
race and compatibility cases. A06 still has concrete error-classification gaps; A01/A03/A07/
A11/A16–A19/A22–A23 retain bounded/partial coverage as documented. No remaining acceptance
checkbox is closed solely by this consolidation.

### 6.2 Implemented and exercised paths

- **Discover/connect/serverinfo and pairing preparation (A01/A02/A04):** real mDNS/IPv4 and
  HTTP bootstrap/HTTPS paired status exist. LAN discovery/manual selection work in the scoped
  user flow; complete scan/custom-port/wider-host coverage remains separate. Main/Search's
  eight-host cap is now removed in source; >8-PC PAF acceptance remains pending. Discovery
  still has its 16-entry backend bound, so broad discovery support remains partial.
- **Pairing, actionable errors and server trust (A05/A06/A22):** five-phase proof/final HTTPS
  challenge, host server.pem pinning and exact-leaf verification are implemented. Normal pairing,
  persisted trust/reconnect, wrong-PIN rejection, genuine 409 presentation, active-wait cancellation,
  pin mismatch rejection/restoration and host-revocation/re-pair have scoped Vita evidence.
  F6's old-host wrong-PIN retry limitation is accepted; malformed-proof/pin-write cases are unverified.
- **Apps (A07/A08):** authenticated /applist, shared Expat parser, cache invalidation and page-owned
  Apps vector exist. Ten rows/>8 selection, two-host list switching and empty-state behavior have
  scoped evidence. The backend bound is 64; parser error fixtures and the deferred 64/65 case
  remain unrun. Do not recreate the obsolete eight-app or substring-parser fixes.
- **Launch/resume/RTSP/start (A09–A13):** fresh authenticated host snapshot, different-app conflict
  rejection, selected-app resume or idle launch, validated XML/session URL, real common-c RTSP/
  encrypted RTSP and Vita renderers/input exist. Idle launch, same-app resume, app exit then launch
  and different-app rejection are tested. Slow capture, custom ports and all failure stages are not.
- **Stop/disconnect/reuse (A14–A18/F1/F5):** native owner cleanup, API admission, deferred PAF
  buffer release, idle and active DisconnectHost/reconnect, in-flight HTTP and startup cancellation
  have scoped logs. Stage 4 tests failed-init unwind; stage 10 tests cancelled success with buffers
  allocated, suppressed frontend STARTED and old unmaps before reconnect. Three ordinary cycles
  on system build 98f0d44194af complete all nine GXM unmaps/audio releases and receive A/V.
  Repeated host reconnect, shutdown and arbitrary callback/admission races remain unverified.

Source/media scope remains IPv4, H.264, stereo, one controller and Rec.601 limited range.
Sunshine 2025.924.154138 is identified for specific success/failure runs; 2026.914.233613 is
identified for a separate delayed pending-session recovery. Neither result certifies every
configuration/version. The three ordinary-cycle log identifies its client build, but no current
Sunshine release/playback confirmation was supplied for that exact run. Preserve that distinction.
USER_RW is stable after those three stops; the +464-byte newlib rise is an observation, not a
proven leak or leak-free guarantee. Earlier quality/audio-warning observations remain scoped.

### 6.3 Concrete local gaps — implementation backlog

1. **Snapshot-pressure event delivery: source-addressed; build/Vita pending (F5).**
   The original 16-slot shared ring dropped every event when full; Connect/Apps retained worker
   results but pairing/stream completion had no fallback. The correction in F5 separates two
   replaceable snapshots from the ordered FIFO and retains the extracted completion until its
   service is ready. Bursts now preserve pairing/stream/closure, copied payloads and order in
   source checks. An ordered-only burst exceeding 16 queued events is still rejected/logged;
   this bounded policy relies on the sequential foreground workflow, not an arbitrary lossless
   event bus. Broader stale-session routing remains separate. Do not mark Vita behavior fixed
   until the maintainer's ordinary smoke test passes.
2. **Host persistence errors: source-addressed; build/Vita pending (L8 / architecture finding 4).**
   The baseline void writer and unconditional store success matched
   [xyzz's writer](https://github.com/xyzz/vita-moonlight/blob/984603bd6f93f752593048fe494b5ffca14514e1/src/device.c#L231-L265),
   re-read for this correction. Now save_device_info returns 0/-1, checking fopen, sticky ferror
   after writes and fclose's buffered flush. Every opened file closes even after a write error;
   failures log path/stage/errno. Add/MarkPaired/SetHostId return the status and UUID-saved logs
   require success. Search's existing native "Unable to save PC" dialog receives Add failures.
   Successful connection/pairing still complete normally with separately logged persistence
   failure; pin persistence remains separate. Normalization logs failed rewrites, retaining usable
   in-memory hosts. Global identity, exact record resolution, paths and INI fields are unchanged.
   Eight new source checks inject open/every-field-write/close/directory failures, exercise new/
   existing records and successful pairing despite metadata failure. Previous source fails 24
   cases across six tests; all 108 current Python checks pass. No actual filesystem, native-dialog,
   compilation or device proof. Existing memory mutations remain on failure; non-atomic truncating
   writes may leave partial files. Atomic replacement, rollback and power-loss durability are not
   addressed. Maintainer build/save/restart smoke and controlled storage-failure tests remain open.
3. **Host presentation and discovery service: source-addressed; build/Vita pending (A01).**
   Main/Search now read dynamically sized snapshots. Count/copy uses the existing service API;
   discovery's total-count return triggers a resize/retry on growth. Old cells are removed before
   vector swaps; fresh tokens in widget names resolve through page-only userdata, following
   [BHBB EntryCB/factory](https://github.com/Ibrahim778/BetterHomebrewBrowser/blob/9e615e9186bf7ca5fb985dc06c1a5e06c47f24f5/src/pages/app_browser.cpp#L410-L416).
   Main remaps selected flags by UUID/MAC/address-port, rejecting stronger identity conflicts;
   Search canonicalizes against the complete saved snapshot by address/port, never a same-name
   fallback. PAF row templates, modal workflow, initial Main refresh, storage and protocols remain.
   Nine new actual-source checks cover 40 saved/16 discovered hosts, growth/shrink/empty/error,
   old row tokens, selection/reorder/Select All/deletion and lookup past eight. All 117 non-build
   checks pass; no build, allocator, real concurrent snapshot or PAF/Vita proof. More rows still
   consume finite Vita memory; discovery retains its 16-host backend bound. Maintainer >8-PC
   scrolling/selection/Delete/navigation acceptance is pending.
   **Discovery lifecycle follow-up, 2026-10-22:** the backend already owns the scan worker.
   Removing the redundant PAF DiscoveryJob and service m_running flag removes the stale natural-
   completion cache and queued-start-after-Stop path. Lifecycle calls are serialized on the main
   thread; network polling stays in the native worker. Search receives start errors synchronously
   through its existing native dialog, with no second queued HOST_SCAN_FAILED. Repeated Start
   during an active scan is a no-op. Stop also reaps naturally finished workers; restart joins the
   old UID before replacement because FINISHED precedes thread return. CreateThread errors keep
   their exact result. The pinned V check_host.c native-worker polling/terminal-state pattern was
   re-read; no SDL GUI, repeated-thread array or new protocol was copied.
   Nine new source tests cover natural completion/restart, early Stop, finish-before-return,
   exact start errors with one caller error, cancellation/cleanup and a transient wait timeout.
   All 126 non-build checks pass; three fail with the previous native start code. No compiler/ABI,
   real kernel/socket/PAF or Vita proof. Existing volatile state, socket-init error coverage and
   permanent wait-error handling remain separate concerns; the inherited Stop wait loop is not
   given a new timing guarantee. Next: maintainer build and ordinary Search reopen/connect smoke.
4. **Narrow error/compatibility limits (A03/A06/A23).** Some local pairing failures still classify
   as protocol errors, capabilities remain fixed and version bounds are bypassed. Address a
   concrete required case with its own source/host evidence; no speculative codec/timeout expansion.

### 6.4 Requested but blocked, optional and excluded

- **Self-unpair on Delete (A21): missing, upstream protocol prerequisite.** Local Delete works;
  requested remote self-revocation is unavailable under the no-admin-login constraint. Current
  Sunshine [GameStream route/handler source](https://github.com/LizardByte/Sunshine/blob/master/src/nvhttp.cpp)
  was rechecked 2026-10-21: serverinfo/pair/applist/appasset/launch/resume/cancel, no self-unpair.
  Its [admin unpair](https://github.com/LizardByte/Sunshine/blob/master/src/confighttp.cpp) requires
  separate authorization/CSRF and the server-side client UUID. Wait for a supported client
  operation; do not add an invented endpoint, host patch or global credential rotation.
- **Remote Quit (A20): intentionally unsupported.** Current Sunshine /cancel terminates sessions/
  process and restores display configuration. [Moonlight Qt quitApp](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/backend/nvhttp.cpp)
  sends /cancel; the function name does not make /quit a route. Rechecked 2026-10-21. No requested
  remote-Quit UX exists here; normal stop/cancel does not acquire that host-wide meaning.
- **App artwork (A24): intentionally unsupported** for the current text list. **Sunshine admin
  API: not applicable** to the agreed client scope. HEVC/AV1/HDR/surround/multi-controller/IPv6
  are outside current media/address scope; they are not missing ordinary client HTTP calls.

### 6.5 Deferred verification — not the next feature task

Keep T1–T10 as a milestone/release catalogue with each existing result attached. For now defer
new automatic cancellation checkpoints, active-app shutdown triggers, blocked-I/O latency tests,
long stress/leak attribution and exhaustive host-side worker/input/display inspection. They remain
unverified; no evidence is erased. Do not keep the normal API path perpetually "not implemented"
because these tests have not run. A new regression/log can reprioritize a specific case.

Before broad release claims still verify malformed/truncated/error XML with the existing C
fixtures, malformed/unreadable pins and proof/crypto failures, pin/metadata write failures,
custom HTTP/HTTPS/RTSP ports, mandatory-encryption policy and actually claimed server versions.
The 64/65-app boundary remains explicitly deferred by the maintainer. No trusted-path downgrade,
TLS weakening, delayed retry or host workaround is proposed to reduce testing scope.

### 6.6 Completion gates and immediate next step

- **Inventory gate: complete.** A01–A24 record semantics/auth/source/upstream/status/evidence;
  GameStream and admin scopes are distinct. Dated follow-ups preserve actual Vita evidence.
- **Current scoped client path: implemented and exercised as listed, with explicit limits.**
  This does not close local persistence/event/discovery defects or external self-unpair.
- **Queue source gate: snapshot-pressure correction and 100 non-build checks complete;
  maintainer build/Vita gate open.** No arbitrary critical-storm/race guarantee.
- **Persistence source gate: checked write/close status through host store; 108 non-build checks
  complete; maintainer build/Vita gate open.** No atomic-write/durability guarantee.
- **Host presentation source gate: dynamic snapshots/token callbacks and 117 non-build checks
  complete; maintainer build/Vita gate open.** Backend discovery capacity is still 16.
- **Discovery source gate: direct lifecycle/status, exact start errors and native reaping;
  126 non-build checks complete; maintainer build/Vita gate open.** Network polling stays threaded.
- **Next priority: maintainer build and Search completion/reopen → connect → Apps smoke.**
  Further narrow API changes require a concrete case; no speculative endpoint or test expansion.
- **Release/compatibility gate: open.** Deferred error/security/version/shutdown/stress tests,
  broader shared-state ownership and the separate build/license requirements remain pending.

### 6.7 Separate Main/UI follow-up

Main cold-start follow-up (2026-10-19): the user confirmed that one saved PC appears immediately
after cold start without connecting or opening Settings. The supplied Vita log reports
`hosts=1 cells before=1 after_delete=0 after_insert=1 selection=0`. This verifies that visible
one-host scenario on the tested build; the installed revision and initial-refresh completion line
were not supplied. Empty/multiple-host cold starts remain open, and this result does not isolate
`10c62e14d66061d3434b3996779e23b28c447bcc` as the causal change. See ROADMAP for the canonical result.
The user explicitly confirmed the completion line is absent. In current source the logger starts
disabled in `src/common.cpp`; the saved Debug log preference is applied only on lazy AppSettings
initialization. Main's startup refresh can finish before logging becomes enabled, so absence of
that line alone does not demonstrate refresh failure. Enabling Debug log in a previous process
does not guarantee early-startup capture. No logging or refresh code was changed for this finding.
`Main::InitialRefreshTask()` was not changed. Analyze any additional cold-start logs before changes:
`hosts == 0` points below PAF to storage/load; `hosts > 0` with mismatched cells points to ListView
lifecycle; matching counts with invisible rows points to layout/template/activation state.
