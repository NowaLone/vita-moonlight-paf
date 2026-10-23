# Repository simplification audit — 2026-10-23

Requested scope: inspect the whole project and apply focused simplification and
optimization using Ponytail. Work is on `codex/simplify-implementation`, starting
at `a95d465`. The earlier confirmed remote-Quit feature and its reported Vita
acceptance precede this audit.

Through source commit `bcbf30d`, the owned `src/`, `include/` and `cxml/` diff has
128 inserted and 479 deleted lines across 31 files: **351 fewer lines**. New
regression checks and documentation are counted separately. All 183 non-build
checks pass, up from 167 at the baseline. No new dependency was introduced.

## Scope and decisions

Reviewed repository-owned C/C++, headers, CXML, build scripts/CMake integration,
focused checks and development documentation. Traced unused declarations through
their callers and exports before removing them. Compatibility-sensitive changes
received separate review. Vendored dependencies were reviewed at their integration
boundary, not rewritten or independently audited line by line.

- **Pages/resources:** checked page state, row/template ownership, native dialogs,
  navigation and CXML references. Removed inactive state/styles and duplicate
  initialization; kept active geometry, selection templates and focus behavior.
- **Application/services/backend:** checked service callers, foreground ownership,
  main-thread dispatch, network bootstrap and app-cache invalidation. Removed
  unused API chains and reused cleanup without collapsing the required layers.
- **Host store/discovery:** checked identity/metadata merging, allocation and string
  boundaries, discovery lifecycle and DNS decoding. Reused existing merge logic
  and corrected concrete bounds problems without changing persistence formats.
- **GameStream/security:** checked protocol/error propagation, worker admission,
  paired TLS trust and credential ownership. Kept the global client identity,
  per-host certificate pin, typed pairing errors and remote-Quit confirmation.
- **Streaming/input:** checked session ownership, setup/teardown, frame lifetime,
  video scaling, audio, controller/touch/motion and statistics. Shared contact and
  scaling logic; retained asynchronous transport and deferred frame release.
- **Build/dependencies:** inspected the two-profile build and explicit dependency
  lists; shell syntax checked without running either script. No compiler flags,
  heap budgets, SDK binding, dependency or packaging behavior was changed.

## Applied simplifications

Each logical change has a separate commit:

- `924a6a5` removes unused settings/host compatibility chains, obsolete legacy
  declarations and unused video getters. The executable exports only its module
  entry, so these were internal unused APIs, not externally exported interfaces.
- `2812221` removes Main/Apps selected-index storage, Pairing title/button storage
  and five unreferenced CXML styles. Resource/text-object checks cover references.
- `0a60782` reuses `ShutdownNetwork()` for initialization failures, preserving the
  NetCtl → Net → memory → module cleanup order and original failure results.
- `5632e70` invalidates the application cache using validity/count instead of
  clearing all 64 records. Readers already gate admission, validity and count;
  shorter refreshes cannot expose stale tail entries.
- `1bc7b2c` shares DS4/tablet contact tracking and dispatches their protocol events
  through one small helper. The old tablet path did not retain coordinates:
  stationary frames emitted repeated Move events and a move to the origin could
  be missed. The common path retains coordinates and fixes both cases.
- `729c324` passes requested video dimensions directly into setup's size calculation
  instead of storing them in two unnecessary globals.
- `3b08e25` initializes Pairing dialog parameters once in `ShowSystemError()`.
  The installed VitaSDK `sceMsgDialogParamInit()` clears the full structure;
  the earlier clears were redundant. Native messages/lifecycle remain unchanged.
- `5a35abc` selects the fit/cover scaling axis once instead of duplicating four
  branches. Zero/equal cases, 64-bit products, ceiling division and alignment remain.
- `6a010f5` reuses `merge_device_info()` when appending an existing saved host;
  UUID/storage identity retain their fill-only behavior.
- `f3712fc` removes unused page `Id()`/`Root()` accessors.
- `3b73862` uses the discovery count as the next host ID, removing a second
  counter that always reset and advanced with it. IDs and publication order remain.
- `cb82573` removes the unused 512-byte identity-path copy from stream worker
  arguments and its private helper parameters. Public start admission still
  rejects a null path; API directory lookup/errors/logging remain. Requests use
  the already loaded client ID, as before.
- `bcbf30d` skips 544 no-op row `memmove` calls for an exact 960×544 center-mode
  framebuffer. Larger surfaces retain the original in-place center crop;
  presentation dimensions/pitch and statistics remain outside the copy guard.

The intended behavior changes are concrete correctness fixes, separate from the
behavior-preserving cleanup:

- `2619352` rejects malformed/truncated mDNS names and record data before use.
  Pointer traversal is bounded by packet length, so cyclic DNS compression cannot
  hang discovery; pointer/label/header/RDATA reads are checked. Valid compressed
  Question/PTR/SRV/A processing remains covered. The wire-format reference is
  [RFC 1035, message compression](https://www.rfc-editor.org/rfc/rfc1035.html#section-4.1.4).
- Tablet contact-history correction is included with the shared tracker above.
- `215b694` terminates new-host name/internal/external fields even at their full
  255-character capacity. The old copy could leave an uninitialized final byte;
  the regression check uses nonzero destination storage to expose it.
- `26316a9` returns INTERNAL when GameStream worker attributes/stack/creation fail.
  Previously these branches ran crypto/curl inline on the smaller PAF caller
  stack. Attribute cleanup and successful join/callback results remain unchanged.
- `953cedb` escapes reserved characters in two locale XML attributes. The decoded
  labels remain identical; the file now passes XML parsing with the other resources.

Source-proven work removed includes bulk cache clearing, repeated stationary
tablet Move sends, duplicate field/size calculations and exact-size crop copies. No Vita timing, FPS,
power, heap-usage or executable-size improvement is claimed without measurement.

## Complexity retained deliberately

The page → service → backend → adapter boundary is a maintainer requirement and
keeps PAF out of networking/protocol code. Services with one backend are not a
reason to call legacy globals from pages.

Worker completion, cancellation, ordered events/snapshots, stream ownership and
deferred framebuffer release have different lifetime contracts. Combining their
flags or cleanup paths solely to shorten code would obscure these contracts.
Likewise, audio drain and independent motion sampling are device concerns.

Native AppSettings/IME/message/busy dialogs retain their own lifecycles. The
active selection-row template is distinct from the normal row template. Host
identity resolution and certificate pinning are security/persistence rules, not
optional fallback boilerplate.

Fixed discovery/application limits and the small bounded host-list scans were
retained. Replacing them with maps, frameworks or a new dependency has no measured
benefit here. Compiler optimization switches, priorities and memory budgets need
controlled native profiling, not a convenience refactor.

## Verification and remaining work

Baseline: 167 passing Python non-build checks; final source gate: **183 passing**.
Network cleanup, cache, host merge and video scaling checks passed both before
and after their refactors. The updated gate and individual
check limits are in [tests/README.md](../tests/README.md). Checks interpret selected
actual source with mocked calls; XML checks parse resources, and `bash -n` checks
the scripts. Diff whitespace checks pass. No application/resource build, CMake
configuration, packaging, deployment or C/Expat fixture compilation was run.
The follow-up additionally checks null-path admission and byte-exact crop output
for exact/wider/taller surfaces with statistics enabled/disabled. GPU and PAF calls
remain mocked; this is not a measured native speedup.

Maintainer validation of this audit's changes is still pending:

1. Build both profiles and rebuild CXML; verify startup, host-list/selection,
   Settings and pairing error dialogs.
2. Exercise discovery, saved-host add/restart, paired reconnect, Apps and confirmed
   Quit → selected launch. Check failed network/worker initialization where practical.
3. Check fit/center-region output, DS4/tablet Down/Move/Up (including stationary
   contact and the origin), normal controller/audio, PS stop and repeated restart.

Earlier Vita evidence remains scoped to its recorded source/features. This audit
does not close existing ABI, allocation, TLS, threading, storage-failure, endurance
or release gates in [ROADMAP.md](../ROADMAP.md).

The README no longer instructs running an absent `dev-build-run.sh`; only
`build.sh` and `build_cxml.sh` are shipped. Exact SDK/PAF/CXML revisions and a
reproducible setup procedure remain the next independent roadmap task.

The two initially deferred candidates (worker path copy and exact-size crop
copies) were completed in the follow-up above. Further optimization should start
with native profiling; the retained lifecycle/security contracts still apply.
