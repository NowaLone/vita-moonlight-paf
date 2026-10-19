# Sunshine / GameStream client API audit

Audit date: **2026-10-18**. Local baseline: `master`,
`761d730f53a30dc8201286f2d10e22a58a1c609a`; clean working tree before this documentation change.

This continues [the preliminary audit](../ROADMAP.md#gamestream-client-api-coverage-and-follow-up-2026-10-15)
and the findings in [REPOSITORY_ANALYSIS.md](../REPOSITORY_ANALYSIS.md). It is a **source-level audit**,
not a new Vita/Sunshine interoperability test. No runtime code, dependencies, settings or host-storage
formats were changed. `PROGRESS.md` was absent. The supplied PRD was read as technical context;
current source and Git state determine the findings below.

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
validate GameStream XML status. L3 uses Expat for status/fields; L1 app lists and L2 launch use
separate string scans with weaker checks (F3).

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
- **Current:** L3 `load_server_status/load_server_info` learns HTTPS port through HTTP, tries
  HTTPS, then falls back to HTTP; L1 connects and persists server UUID. Missing required fields fail.
- **Upstream / Sunshine:** QH/V use serverinfo and BUSY-state normalization; S returns server
  metadata and process state. This implementation's broad HTTP fallback can hide an HTTPS problem;
  it must not be interpreted as authenticated status or certificate validation (A22).
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

### A05 — Pairing handshake — implemented (with authentication gaps in A22/F4)

- **Endpoint / purpose / auth:** HTTP `/pair` phases `getservercert`, `clientchallenge`,
  `serverchallengeresp`, `clientpairingsecret`; HTTPS `/pair?phrase=pairchallenge` proves client trust.
- **Semantics:** send salt/client certificate; receive hex server certificate; exchange AES-ECB
  challenges and signed secrets. Gen 7 uses SHA-256, older generations SHA-1. Check XML status and
  `paired=1` at each phase. Host operator supplies the displayed PIN through Sunshine UI.
- **Current:** L3 performs all phases, RSA-2048/signature-size assumptions, initial wait 300 seconds,
  later timeout 15 seconds; L1 marks paired and keeps persistence failure distinct from protocol success.
- **Upstream / Sunshine:** QP/V use the same handshake family; S implements phased pairing,
  expiry and failed-session removal. QP additionally pins the server certificate.
- **Verified / runtime:** source; T3 covers correct PIN, delayed approval, retries/expiry and final
  HTTPS trust. Five-minute timeout is a client setting, not verified responsiveness on Vita.

### A06 — Pairing errors — partial

- **Endpoint / purpose / auth:** errors from A05; distinguish PIN mismatch, security, network,
  protocol and local failure without presenting storage failure as bad PIN.
- **Semantics:** XML rejection/`paired=0` differs from curl failure and cryptographic proof failure.
- **Current:** L1 `classify_pairing_error` recognizes exact legacy error strings, selected curl
  errors, and OOM. Other local failures fall through to PROTOCOL. L3 checks challenge hash before
  signature, whereas QP verifies signature first; corrupted/untrusted proof can be classified too
  early as PIN mismatch. `verify_signature` also has the return-value issue in F4.
- **Upstream / Sunshine:** QP has explicit failed/PIN/in-progress results; S can reject ordering,
  pending capacity, invalid proof or expiry. These are not all incorrect-PIN cases.
- **Verified / runtime:** source; T3 requires wrong PIN, refused approval, lost network, malformed
  proof and local crypto/storage failures. Existing semantic result enum alone does not prove coverage.

### A07 — Application list retrieval — partial

- **Endpoint / purpose / auth:** paired HTTPS `GET /applist?uniqueid=...&uuid=...`; list launchable apps.
- **Semantics:** XML root status plus repeated `<App>` containing `<ID>`, `<AppTitle>` and optional
  capability data; status failure is not an empty successful list.
- **Current:** L1 reuses authenticated curl handle and refreshes a 64-entry global cache through
  L4 ApplicationService job. Curl failure clears cache; XML status is not checked. Cache validity
  is not cleared on host change/disconnect. L7 displays only eight apps.
- **Upstream / Sunshine:** S emits the configured app list; QH `getAppList` verifies status before
  XML parsing; V uses its XML parser.
- **Verified / runtime:** source; T2/T4 cover revoked client, valid empty list, two hosts with
  different lists and >8/>64 entries. Complete product support is not established by the 64-entry cache.

### A08 — Application list parsing — partial

- **Endpoint / purpose / auth:** A07 response, same paired HTTPS requirements; preserve app identity/title.
- **Semantics:** XML element boundaries, entity decoding, valid numeric IDs, malformed/truncated
  document rejection; entries must not inherit fields from unrelated/incomplete `<App>` elements.
- **Current:** L1 `strchr/strstr/atoi` scans tags, commits on `</App>`, truncates titles, does not
  decode entities, validate full XML/status, or reset fields on each opening `<App>`. Reports cache valid
  even for an unrecognized/error document.
- **Upstream / Sunshine:** QH/V use XML parsing; S serializes app titles as XML, including escaped entities.
- **Verified / runtime:** source; T4 parser fixtures and Vita list rendering remain unrun. Reuse
  the already-linked Expat in an isolated parser change; adding another endpoint will not fix this.

### A09 — Launch — partial

- **Endpoint / purpose / auth:** paired HTTPS `GET /launch`; start selected host app and prepare stream.
- **Semantics:** query includes app ID, `mode=WxHxFPS`, SOPS, random `rikey`/`rikeyid`, local-audio
  choice, stereo surround info, controller bitmap and `corever=1`; reply has status and
  `gamesession=1`, normally `sessionUrl0`.
- **Current:** L2 `load_launch_session` sends these values with 10-second connect/20-second total
  timeout; accepts status-200 text **or** positive session text and invents default RTSP URL if absent.
  It then sets cached `current_game` to the selection. F2/F3 apply.
- **Upstream / Sunshine:** S refuses launch if an app is running and can report capture/launch errors
  in XML; QH verifies status, V checks status/session result. Qt allows a longer launch timeout (120 s).
- **Verified / runtime:** source; T5 must cover slow app startup, unsupported mode, failed capture,
  XML rejection and custom RTSP port. Timeout difference is a compatibility risk, not a proven defect.

### A10 — Resume — partial

- **Endpoint / purpose / auth:** paired HTTPS `GET /resume`; attach to an existing host application.
- **Semantics:** fresh input key/id and stream options; successful reply `resume=1` and session URL.
  S resumes the **already running process**; supplied `appid` does not launch/switch it. No running
  app produces XML 503. Capture/encryption policy may also reject the request.
- **Current:** L2 chooses resume whenever cached `server->current_game` is nonzero, regardless of
  selected app; there is no pre-start refresh. It overwrites cached ID with the selected ID (F2).
- **Upstream / Sunshine:** S `resume`; QS asserts current app is zero or matches selection; QH
  validates response, V uses resume for a running app.
- **Verified / runtime:** source; T5/T8 require same-app resume, different-app selection, app exit
  between connection/start and stop/restart. Do not label arbitrary selected-app restart implemented.

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
- **Current:** L2 stop worker handles an established connection even after setup worker returns;
  repeated request while stopping is ignored. L7 detaches PAF surfaces and defers framebuffer release.
- **Upstream / Sunshine:** L6/C stop contract; S/SS observe control disconnect or timeout and
  clean stream workers; host app can remain running. QS separates transport stop and optional quit.
- **Verified / runtime:** source and historical double-PS stop report; T7/T8 still required for this
  full matrix, including immediate restart and final neutral input. No end-to-end idempotency proof
  under concurrent callbacks; flags are not synchronized. See F1 for unexpected termination.

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
- **Current:** explicit disconnect waits on active/stop flags and defers GameStream free. Involuntary
  `on_connection_terminated` only stops input, emits STOPPED and clears `started`; no LiStopConnection
  follows in L1/L4/L7. This is a concrete ownership gap (F1), not verified cleanup.
- **Upstream / Sunshine:** V and QS both call LiStopConnection on termination cleanup; C's callback
  signals termination, it does not perform its caller's stop. SS handles disconnect/timeouts host-side.
- **Verified / runtime:** source; T7/T8 need network loss, host kill, explicit disconnect and restart.

### A18 — Reconnect — partial

- **Endpoint / purpose / auth:** reconnect serverinfo, then paired applist and fresh launch/resume/RTSP;
  reuse installation identity but generate new session keys.
- **Semantics:** wait for prior teardown, refresh host state; app may persist, exit or change while away.
- **Current:** L1 rejects connect during worker/disconnect activity and refreshes metadata on new
  ConnectHost; same-host restart without reconnect reuses stale current_game. Unexpected termination
  can make the running predicate false without cleanup. Cache is also not invalidated (F1/F2/F3).
- **Upstream / Sunshine:** QS serializes active session cleanup; S retains process independently of
  stream and accepts resume with fresh session material.
- **Verified / runtime:** source; T8 requires stop→restart, disconnect→connect, network-loss→connect,
  app exit/change on host, and two-host cache isolation. No automatic network reconnection is implemented.

### A19 — Host-side session cleanup — partial (end-to-end evidence pending)

- **Endpoint / purpose / auth:** ENet control disconnect/timeout and RTSP launch expiry; no extra
  client HTTP endpoint is required just to release stream resources.
- **Semantics:** distinguish pending launch slot, established stream workers, retained application
  process and display/controller state. Releasing one is not proof the others were reset.
- **Current:** L6 explicit stop gracefully disconnects; pending launch cancellation sends no host
  request. F1 can prevent that teardown on unexpected termination.
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
- **Current:** L3 creates/loads global identity, supplies SSLCERT/SSLKEY and disables SSL session
  cache. Both VERIFYHOST and VERIFYPEER are disabled; pairing's plaincert is temporary and never
  used for subsequent TLS identity checks. No server pin is persisted. F4 applies.
- **Upstream / Sunshine:** QP pins the learned certificate and returns it for persistence; QH
  compares peer certificate on SSL errors; S validates the exact enabled client X.509 identity.
- **Verified / runtime:** source; T3/T9 need certificate mismatch, revoked/disabled client, renewed
  host certificate, damaged credentials and ordinary paired requests. TLS transport alone is not
  proof that the client authenticated the server.

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

### F1 — Unexpected termination loses ownership before common-c cleanup (first follow-up)

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
limits and partial documents; T4/T5. No parser test or code change was performed in this audit.

### F4 — Server TLS identity is not pinned; signature error check is too broad

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
  corrupted local credentials; retry/re-pair behavior. Remote Quit/unpair tests require an explicitly
  requested feature; ordinary stop must leave other clients/apps unaffected by a `/cancel` request.
- **T10 Compatibility:** record each actually tested Sunshine release/configuration; exercise
  encryption policy and IPv4 custom ports. Test older GFE only if support is claimed. Do not infer
  compatibility from `unsupported=true`, fallback constants or successful desktop compilation.

## 6. Completion and next work

The **source inventory and comparison are complete for the stated client scenarios**. End-to-end
API compatibility remains **in progress** until the runtime matrix and required fixes pass.
Missing ordinary-disconnect HTTP endpoints were not found: the required work is correct ownership,
state freshness, parsing and authentication, not adding `/cancel`, `/quit` or admin API calls.

First independent implementation task: **F1, preserve transport cleanup ownership on unexpected
termination**, as an isolated change with meaningful lifecycle tests and Vita/Sunshine regression logs.
F2/F3/F4 are separate follow-ups; F4's small signature check and broader pinning design are distinct work.

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
