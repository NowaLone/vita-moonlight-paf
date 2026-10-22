---
name: vita-moonlight-paf
description: "Work on the Vita Moonlight PAF repository: PS Vita PAF UI, CXML resources, Moonlight/GameStream services and backend, host persistence, streaming lifecycle, Vita input, builds, validation, and roadmap tasks. Use whenever implementing, debugging, reviewing, or documenting this project."
compatibility: Repository-specific skill for NowaLone/vita-moonlight-paf; Vita hardware validation and builds require the appropriate PS Vita SDK/toolchain.
metadata:
  version: "1.0.0"
---

# Vita Moonlight PAF Engineering Skill

Use this skill for safe, repository-aware development of the native PS Vita Moonlight frontend. The repository documentation is the source of truth for current behavior; use this skill to enforce a repeatable workflow and preserve architectural boundaries.

## 1. Start by establishing current state

1. Identify the repository root and current branch. The agreed working branch is `master`.
2. Check the working tree before editing. Do not overwrite unrelated or uncommitted user changes.
3. Read `AGENTS.md`, `README.md`, and `ROADMAP.md` from the current branch before choosing a task. If the task touches the interface (`cxml/`, `src/pages/`, `src/option_menu.cpp`), also read `docs/UI.md` first. Follow more-specific repository instructions and refresh your understanding when a file or behavior may have changed.
4. Inspect the implementation related to the task. Do not assume a prior chat summary, a commit message, or documentation proves the current code behavior.
5. Prefer the next uncompleted, highest-priority item in `ROADMAP.md`, unless the user gives a different task.

If using GitHub-backed tools rather than a local checkout, explicitly read files from `master`, use the current file SHA for each update, and verify the resulting commit and file contents. Do not claim a build or hardware test unless you actually ran it.

## 2. Project architecture

The intended dependency direction is:

```
PAF pages -> MoonlightApp -> Services -> MoonlightBackend
          -> LegacyMoonlightAdapter -> legacy GameStream/Moonlight
```

- `src/pages/`: PAF views, navigation and user interaction.
- `src/app/`: `MoonlightApp`, application lifecycle, backend/service ownership and main-thread event dispatch.
- `src/services/`: host, discovery, pairing, connection, application-list and settings service wrappers.
- `src/backend/`: backend abstraction and implementations.
- `src/backend/legacy/`: compatibility-sensitive legacy Vita Moonlight/GameStream code.
- `src/moonlight/`: streaming session, Moonlight-facing API, Vita video/audio/input paths and `SceAppSettings` adapter.
- `cxml/`: PAF CXML UI/resources, localization and AppSettings definitions.
- `third_party/`: protocol/transport and parsing dependencies.

Keep pages focused on presentation and navigation. Route application behavior through `MoonlightApp`, services and backend interfaces. Backend callbacks must not mutate PAF widgets directly from worker threads; deliver events through the main-thread dispatch path.

## 3. Non-negotiable project rules

- Treat `AGENTS.md` as the detailed architecture/guardrail reference and `ROADMAP.md` as the prioritized plan.
- Make the smallest change that solves the stated problem. Prefer small, independently verifiable commits with imperative messages.
- Do not casually change host/settings persistence formats, storage paths, host identity behavior or global client-identity handling.
- Do not move streaming, protocol or networking logic into PAF pages.
- Treat `src/backend/legacy/` and stream cleanup/resource ownership as high risk; inspect callers and lifecycle before modifying them.
- Asynchronous backend events must reach PAF through the main thread.
- Do not replace real Moonlight/GameStream behavior with an improvised protocol shortcut without checking the existing implementation.
- Keep the app's intended UNSAFE/native system-integration model. `SceAppSettings` is the required settings UI/storage integration; do not resurrect the old `moonlight.conf` model or invent a custom settings file.
- Do not reintroduce saved-host clipboard/copy behavior or an `SceClipboard` dependency unless explicitly requested.
- Do not recreate temporary speech-balloon test pages/styles that have already been removed.
- Do not interpret the current `OptionMenu` as a discovered Sony system action-menu API: it is a project-owned PAF composition.
- Never commit generated build output, VPK/SELF/RCO/object files, private keys, certificates, host credentials, or deployment secrets.
- Do not silence compiler/build failures with unrelated stubs, weak symbols or unrelated changes.
- Desktop compilation alone does not validate PAF UI, Vita graphics/input, networking or lifecycle behavior.

## 4. Current UI facts to preserve

These reflect the documented and hardware-verified current UI; confirm the files if the task touches them. The complete UI reference, with evidence tags for every measured value, is `docs/UI.md`.

- The bottom-right corner button opens a project-owned vertical PAF `speech_balloon` menu.
- The current menu has **Delete** at the top and **Settings** at the bottom. The down-tail texture points toward the corner button.
- Popup placement and Delete/Settings focus behavior have been verified on hardware.
- Saved-host Delete enters multi-selection mode. Selection rows use `template_host_list_item_select` with native PAF `paf::ui::CheckBox` controls.
- The bottom action bar provides Cancel, Select All / Clear All and Delete. Delete uses native `SceMessageDialog` confirmation.
- Selection UI positioning, list height with six or more hosts, action-bar styling, select-all focus, disabled Delete behavior, confirmation and post-delete refresh/focus are documented as hardware-verified.
- A disabled Delete button retains its black background while its label appears gray through native PAF `ButtonBase` disabled state.
- The main page uses `template_list_view_main` to leave room for the bottom bar. Do not switch selection behavior onto the shared generic row template (`template_list_item_generic`, used by Apps).

Do not reopen these completed UI checks as TODO items without new evidence of a regression.

## 5. Host identity, persistence and pairing

- The host store owns saved-PC metadata; Sony AppSettings owns app settings.
- The global client identity is installation-wide under `ux0:data/moonlight/client/`. Do not put client key/certificate material into per-host directories.
- The GameStream/Sunshine server UUID (`host_id`) is the primary stable host identity once known. Identity resolution also accounts for MAC, addresses and an existing name-only fallback; do not delete records by display name alone when a stronger identity is available.
- Discovery entries are temporary. Manual input is persisted only after a successful connection; do not accidentally leave a host record after a failed manual connection.
- Pairing success is distinct from a host-persistence failure. Only `MOONLIGHT_PAIRING_ERROR_INCORRECT_PIN` means the cryptographic PIN check failed; preserve distinctions for transport, protocol, security and internal failures. Preserve typed legacy results; do not infer categories from diagnostic strings. Local allocation/execution failure must not masquerade as a network/protocol/security rejection; all verifier non-success still rejects pairing.
- Check the existing adapter/store flow before touching migration or deletion logic. The project does not currently promise migration from the original Vita Moonlight per-host credential layout.

## 6. Build and validation workflow

### Build

The maintainer runs builds; the agent must not run application/resource compilation, CMake
configure/build, Make, packaging or rebuild/deployment scripts (`build.sh`, `build_cxml.sh`,
`dev-build-run.sh`). The commands in README/AGENTS are maintainer reference only. Review source/diffs
and run applicable non-build checks instead. Leave build and hardware validation pending until the
maintainer supplies results, and distinguish their results from checks run by the agent.

### Runtime validation

Focused non-build Python source checks exist; use `tests/README.md` for the command and limits.
They interpret selected actual source with mocked platform/library calls and do not establish
compilation, ABI, real crypto/networking/PAF or device behavior. Actual C/Expat fixtures require
maintainer compilation/execution. The maintainer validates device behavior by affected layer:

- CXML/layout, PAF focus and widget state: run on a real Vita.
- Discovery, pairing and host store: exercise successful and failed cases and inspect logs.
- Streaming changes: test connection setup, video, audio, controller/touch input, stop/cleanup, return to UI and reconnect.
- Lifecycle/network changes: test loss/recovery and repeated entry/exit, not only the happy path.
- Motion input: the code path exists but is not fully hardware-verified; do not call it verified unless tested on Vita.

Separate completed checks from recommended but unrun checks in the final report.

## 7. Roadmap-driven task order

Use the current work decision in `ROADMAP.md` before historical next-step requests. As of the
2026-10-22 source/runtime review, API inventory/source comparison is complete; ordinary client
paths have scoped Vita evidence. Remaining partial cases are explicit limits or milestone/release
verification, and self-unpair is blocked by the absent upstream client operation under the
no-admin-login constraint. Do not restart the audit or add endpoints/routine stream test variants.

1. **Next independent task: reproducible setup.** Record exact working VitaSDK, PAF-component
   and CXML-tool revisions and dependency/environment setup. The maintainer validates builds.
2. **Concrete correctness work:** prioritize a newly reproduced regression or a source-proven
   required gap; preserve existing session/dialog/host ownership and add focused non-build checks.
3. **Remaining device/release gates:** lifecycle error cases, motion, trust/storage faults,
   compatibility and endurance remain open, separate from the completed normal-flow inventory.
4. **Release readiness:** license/attribution, installation/versioning and a maintainer-run clean
   build/Vita smoke test remain release requirements.

Use `ROADMAP.md` for completion criteria and current checkboxes. Mark work complete only when evidence exists. Do not treat the full roadmap as an invitation to perform unrelated refactors.

## 8. Making and reporting changes

1. State the intended narrow scope internally and inspect the relevant callers/resources before editing.
2. Make a minimal change preserving public interfaces and storage formats unless the task explicitly requires otherwise.
3. Review the diff for unrelated edits, architecture leaks, generated files and secrets.
4. Run applicable non-build checks. The maintainer owns compilation and hardware verification; report those as pending until results are supplied.
5. Update `AGENTS.md`, `README.md`, `ROADMAP.md` or `docs/UI.md` (for any UI change, in the same commit) only when the documented fact, procedure or project status actually changes. Keep one canonical roadmap rather than duplicating its full checklist elsewhere.
6. Immediately commit each completed logical change on `master` after diff review and applicable non-build checks; the maintainer has given standing authorization, so do not wait for a separate commit request. Include only the task's changes. Push and destructive Git operations still require an explicit request.
7. Report: files/behavior changed, the commit or diff link, checks actually run, and unverified behavior/blockers. Be concise and explicit; never imply that a test passed just because code was edited.
