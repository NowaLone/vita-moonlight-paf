# Project Overview

Vita Moonlight PAF is a native PS Vita application shell built with Sony's PAF framework, intended to provide a system-style frontend for `xyzz/vita-moonlight`. The repository currently contains the PAF application bootstrap, page/navigation framework, settings integration through `SceAppSettings`, host discovery and persistence plumbing, service abstractions, and a legacy Moonlight adapter; the long-term goal is to attach the remaining real Moonlight/GameStream functionality without moving streaming logic into the PAF UI layer.

## Repository Structure

- `src/main.cpp` — PS Vita module entry point, PAF sysmodule initialization, main-thread configuration, and C++ allocation overrides.
- `src/app/` — PAF runtime bootstrap and `MoonlightApp`, which owns backend/services and application lifecycle.
- `src/pages/` — PAF page implementations and page-stack/navigation behavior.
- `src/services/` — Service-layer wrappers for hosts, discovery, pairing, connections, applications, and settings.
- `src/backend/` — Backend abstraction implementations; `legacy_moonlight_adapter.cpp` bridges the service layer to the legacy core.
- `src/backend/legacy/` — Vendored/ported legacy Vita Moonlight code, including device storage, configuration compatibility, host discovery, and mDNS support.
- `src/moonlight/` — Thin Moonlight-facing API and `SceAppSettings` integration.
- `include/` — Public C++ headers for application, backend, services, pages, and Moonlight types.
- `cxml/` — PAF CXML UI resources, settings XML, locale files, and textures. `cxml/vita_moonlight_ui.xml` defines the main application pages.
- `third_party/inih/` — Vendored INI parser used by the legacy device storage implementation.
- `build/` — Generated build directory containing the executable, SELF, and VPK; ignored by Git.
- `build.sh` — Full CXML + CMake + Make build.
- `build_cxml.sh` — Converts `cxml/vita_moonlight_ui.xml` to an RCO.
- `dev-build-run.sh` — Cleans, builds, and deploys the SELF to a PS Vita through `psp2shell_cli`.
- `exports.yml` — Vita module export configuration.
- `psvitaip.txt` — Optional local Vita IP used by `dev-build-run.sh`.
- `vita-moonlight.code-workspace` — VS Code workspace configuration for this repository and related Vita projects.

## Build & Development Commands

### Installing dependencies

The repository does not contain a dependency-install script or package-manager configuration. Required external components are VITASDK, `vitasdk-paf-component`, and `psp2cxml-tool`.

```bash
> TODO: Install/configure VITASDK, vitasdk-paf-component, and psp2cxml-tool using the environment appropriate for the development machine.
export VITASDK=/path/to/vitasdk
```

`build_cxml.sh` also accepts `PSP2CXML_TOOL` when the compiler is not found in its configured fallback locations.

### Building/Running the project

Full build:

```bash
export VITASDK=/path/to/vitasdk
bash build.sh
```

The build produces:

```text
build/vita_moonlight_paf
build/vita_moonlight_paf.self
build/vita_moonlight_paf.vpk
```

Rebuild CXML and then build manually:

```bash
bash build_cxml.sh
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Development build and deploy to a Vita:

```bash
PSVITAIP=192.168.1.123 ./dev-build-run.sh
```

Alternatively, `dev-build-run.sh` reads the first non-comment line from `psvitaip.txt` when `PSVITAIP` is not set. It requires `psp2shell_cli` and a target application already installed with title ID `VLMP00001`.

### Running tests

No test framework, test directory, `CTest`, or test command is present in the repository.

```bash
> TODO: Add a repository-supported unit/integration test command when automated tests are introduced.
```

For current development, validation is performed by building the VPK/SELF and running the application on a PS Vita.

### Running linters and formatters

No repository-configured linter, formatter, `clang-format`, `clang-tidy`, `cppcheck`, or equivalent command is present.

```bash
> TODO: Add a repository-supported lint/format command if a formatter or static-analysis configuration is introduced.
```

## Code Style & Conventions

- C and C++ are both used. Legacy low-level code in `src/backend/legacy/` is predominantly C; application, services, pages, and adapters are predominantly C++.
- C++ classes use PascalCase names such as `MoonlightApp`, `HostService`, and `LegacyMoonlightAdapter`.
- C++ methods and functions generally use PascalCase for class methods and snake_case for free helper functions, matching the existing code.
- Source filenames use lowercase snake_case, for example `page_search.cpp`, `moonlight_app.cpp`, and `host_discovery_service.cpp`.
- Headers use include guards in project C++ code; some legacy headers use `#pragma once`.
- PAF UI object IDs and CXML resource IDs use lowercase snake_case such as `page_search_pcs`, `btn_search_pcs`, and `text_search_status`.
- The PAF UI uses a 960×544 center-origin coordinate system; `pos="0,0"` represents the screen center and positive Y points upward.
- `page::Base` owns the page stack and generic back-button behavior. New PAF pages should follow the existing `Base` pattern rather than implementing independent page-stack management.
- UI pages communicate with Moonlight functionality through `MoonlightApp` services/backend interfaces rather than directly accessing legacy globals.
- `src/backend/legacy/` is C code compiled into the final C++ executable. C functions exposed to C++ must retain C linkage through `extern "C"` declarations where required.
- `MoonlightApp` is responsible for backend lifecycle and main-thread event dispatch; asynchronous backend callbacks should not directly mutate PAF UI from worker threads.
- Settings are stored through the Vita `SceAppSettings` service. Do not introduce a custom settings file or `moonlight.conf` compatibility layer without an architectural decision.
- The build uses explicit compiler flags in `CMakeLists.txt`, including `-fno-rtti`, `-fno-exceptions`, `-fshort-wchar`, warnings, and `-O0` in the configured C/C++ flags. Changes must remain compatible with those settings.
- Recent commits use short imperative messages such as `Add ...`, `Implement ...`, `Fix ...`, and `Restore ...`. No formal commit-message specification is documented.

## Testing Strategy

The repository currently has no automated test suite. There are no unit-test, integration-test, or end-to-end test directories or framework configuration.

Current verification is primarily on-device:

```bash
bash build.sh
```

and, for development deployment:

```bash
PSVITAIP=<vita-ip> ./dev-build-run.sh
```

Changes involving CXML must rebuild the RCO before the application build. Changes involving PAF runtime, networking, discovery, persistence, or UI event dispatch should be validated on actual Vita hardware.

> TODO: Define automated tests and a test directory structure before requiring agents to add new test files.

## Agent Guardrails & Boundaries

- Do not commit generated files from `build/`, `*.vpk`, `*.self`, `*.velf`, `*.rco`, `*.obj`, `*.o`, or `*.a`; these paths/extensions are ignored by `.gitignore`.
- Do not replace the existing PAF architecture with direct page-level access to legacy Moonlight globals. Route functionality through `MoonlightApp`, services, `MoonlightBackend`, and the legacy adapter.
- Do not move streaming implementation into the PAF plugin layer. The existing architecture requires Moonlight functionality to be reached through the `moonlight_api_*` boundary/backend instead.
- Do not bypass the existing main-thread event dispatch mechanism when forwarding asynchronous backend events to PAF UI code.
- Treat `src/backend/legacy/` as compatibility-sensitive code. Changes there can affect C linkage, Vita system APIs, persistent device data, and existing Moonlight behavior.
- Do not change the persistent host/settings formats or storage locations as a convenience refactor without checking compatibility implications.
- Do not commit private keys, certificates, device credentials, Vita deployment credentials, or other secrets.
- Do not bypass build failures or suppress linker/compiler errors by adding unrelated weak symbols or stubs merely to make the build pass.
- Do not delete or rewrite vendored `third_party/inih` code unless the dependency itself is intentionally being replaced.
- Do not assume a desktop build is equivalent to a Vita runtime test; VitaSDK/PAF behavior must be validated on hardware for platform-specific changes.
- There is no documented mandatory review policy, CI requirement, or repository-specific rate limit.

> TODO: Define an explicit list of repository files that require mandatory human review before modification.

## Missing Information / TODOs

- > TODO: Document the supported VITASDK version and the exact required `vitasdk-paf-component` and `psp2cxml-tool` revisions.
- > TODO: Document a reproducible dependency-install/setup procedure for a clean development machine.
- > TODO: Add and document automated unit/integration tests.
- > TODO: Add and document the repository's preferred formatter/linter/static-analysis tooling.
- > TODO: Document mandatory human review requirements for architecture, persistence, networking, and legacy-core changes.
- > TODO: Add a release procedure, including the required `LICENSE` noted by the current README before shipping a release.
