# Sunshine / GameStream client API audit

Audit date: **2026-10-18**. Local baseline: `master`,
`761d730f53a30dc8201286f2d10e22a58a1c609a`; clean working tree before this documentation change.

This continues [the preliminary audit](../ROADMAP.md#gamestream-client-api-coverage-and-follow-up-2026-10-15)
and the findings in [REPOSITORY_ANALYSIS.md](../REPOSITORY_ANALYSIS.md). It is a **source-level audit**,
not a new Vita/Sunshine interoperability test. The original inventory changed no runtime code or
storage; dated source follow-ups below describe later implementation changes (latest: 2026-10-20). `PROGRESS.md` was absent. The supplied PRD was read as technical context;
current source and Git state determine the findings below.

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
  deduplication. Main/Search expose at most eight; HostDiscoveryService keeps `m_running` true
  after natural completion until Stop.
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
  T3 still needs refused approval, lost network, malformed proof and local
  crypto/storage failures; semantic enum presence alone does not establish those paths.

### A07 — Application list retrieval — partial

- **Endpoint / purpose / auth:** paired HTTPS `GET /applist?uniqueid=...&uuid=...`; list launchable apps.
- **Semantics:** XML root status plus repeated `<App>` containing `<ID>`, `<AppTitle>` and optional
  capability data; status failure is not an empty successful list.
- **Current:** L1 reuses authenticated curl handle and refreshes a 64-entry global cache through
  L4 ApplicationService job. The 2026-10-20 source follow-up validates XML status and invalidates
  the cache on connect/disconnect/shutdown and failed refresh. Requests are rejected during stream
  setup/streaming/disconnect. L7 still displays only eight apps; the backend remains bounded at 64.
- **Upstream / Sunshine:** S emits the configured app list; QH `getAppList` verifies status before
  XML parsing; V uses its XML parser.
- **Verified / runtime:** source; T2/T4 cover revoked client, valid empty list, two hosts with
  different lists and >8/>64 entries. Complete product support is not established by the 64-entry cache.

### A08 — Application list parsing — partial

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

### A09 — Launch — partial

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

### A10 — Resume — partial

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
  pending/active separately, but L4 adapter eagerly reports STREAMING after worker enqueue and
  DISCONNECTED before deferred cleanup. No ongoing host-state refresh or exposed durable snapshot.
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

### A15 — Disconnect while idle/paired — partial

- **Endpoint / purpose / auth:** local DisconnectHost; drop local host connection/credentials handle;
  no remote quit/unpair request.
- **Semantics:** CONNECTION_CLOSED should mean local cleanup completed; does not remove saved
  metadata, revoke pairing or kill the host app.
- **Current:** L1 schedules cleanup, then frees L3 curl/crypto state and emits CLOSED; duplicate
  pending disconnect returns success. L4 marks adapter disconnected immediately. No page caller of
  ConnectionService::Disconnect was found; closing Apps only restores the previous page callback.
- **Upstream / Sunshine:** QS/V distinguish local termination from quit; S needs no special HTTP
  disconnect for an idle client. This is an API capability, not a tested UI disconnect feature.
- **Verified / runtime:** source; T7 needs a backend-level trigger, repeat-disconnect and reconnect
  after CLOSED, including cleanup worker creation failure.

### A16 — Disconnect during pending launch — partial

- **Endpoint / purpose / auth:** local cancellation while paired HTTPS launch/resume is outstanding.
- **Semantics:** requesting stop cannot retract an HTTP request already accepted by the host.
  Cancel local continuation into RTSP; wait for the worker before freeing its server pointer.
- **Current:** L2 sets cancel flag and checks after HTTP/before LiStartConnection; L1 deferred
  disconnect waits for `moonlight_stream_is_running`. Curl is not aborted by cancel; a late-cancelled
  setup may briefly emit STARTED before asynchronous stop. Cancelled setup returns STREAM_FAILED.
- **Upstream / Sunshine:** C offers `LiInterruptConnection` during pending common-c startup; SR
  expires pending launch session by configured `ping_timeout`. The remote process may still run.
- **Verified / runtime:** source; T7 must trigger during HTTP wait, between HTTP/RTSP and inside
  common-c startup; verify ordered events and no premature release. Never substitute `/cancel` for
  local cancellation: its scope is host-wide (A20).

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
  established-stream failure/restart, not DisconnectHost/ConnectHost coverage. Host kill, explicit
  host disconnect, repeated failures and setup races still need T7/T8.

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
  after unexpected control disconnect are user/log verified on 2026-10-20. Full host
  disconnect→connect, two-host cache isolation, repeated failure cycles and the remaining T8
  cases remain open. No automatic network reconnection is implemented.

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
This fixes request-buffer ownership, not all asynchronous API state races (F5). Backend/UI limits
remain 64/8. C grammar checks passed; C parser fixtures and Vita/Sunshine tests remain unrun.
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

### F5 — Async API state and reachability qualify lifecycle coverage

**Root cause:** L4 eagerly changes adapter state on enqueue/stop/disconnect; L1/L2 workers and
callbacks update globals without a synchronization owner. L7/L4 event delivery can lag and
MoonlightApp's 16-event queue silently drops overflow. ConnectionService exposes Disconnect but
no page calls it. Closing a PAF page is not necessarily protocol disconnect.

**Minimal direction:** keep durable backend operation state and terminal cleanup ownership, update
observable state on actual completion, make important event loss diagnosable, and use a backend-level
test trigger for currently unreachable disconnect scenarios. Do not add a new disconnect UI only
to make an audit row green. Respect N/B page-lifetime patterns and main-thread dispatch.

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
removed; no runtime code was changed for this issue. Preserve client identity,
authentication and error classification. Continue independent client API work.

## 5. Required validation (all new cases pending)

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

The **source inventory and comparison are complete for the stated client scenarios**. End-to-end
API compatibility remains **in progress** until the runtime matrix and required fixes pass.
Missing ordinary-disconnect HTTP endpoints were not found: the required work is correct ownership,
state freshness, parsing and authentication, not adding `/cancel`, `/quit` or admin API calls.

F1 already has a source cleanup owner (`0cef1ac`). F3 XML/cache and F4 signature checks now have
source follow-ups; runtime acceptance remains pending. F2 now has a source refresh/conflict check.
Per-host TLS pinning (F4) now has a source implementation with the maintainer-approved
`server.pem` contract; old records without a pin require pairing again. The user confirmed the
normal pairing/Apps/restart/reconnect/launch/stop smoke sequence on 2026-10-20, as scoped above.
Different-app conflict, same-app resume and launch after host-app exit without reconnect (T5/F2)
are now user/log verified. F1 established-stream unexpected-control-disconnect cleanup and manual
resume are also log verified; the maintainer separately confirmed audible audio. TLS rejection
of a mismatched valid saved server certificate (-10), followed by restored-pin connection and
authenticated applist (4/4), is now user/log verified. Recovery through PIN pairing after the
host-side revocation test is also log verified; reconnect without a PIN after restarting
Moonlight is subsequently user-confirmed. Wrong-PIN rejection (-1001), error/new PIN and fresh
retry initiation are now user/log verified, but the next request subsequently fails with an
empty HTTP response/-1002 after submitting the new PIN (F6). The maintainer accepts
this as noncritical; Sunshine patches and client workarounds are out of scope. The next
independent implementation task is scalable app-list presentation (64 backend / 8 UI).
Remaining setup-cancel/race, trust/error and
repeated-recovery cases stay open. Complete the remaining T3/T4/T5/T8/T9 error/trust matrix. No
administrative Sunshine credentials are introduced.

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
