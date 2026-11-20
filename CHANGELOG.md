# Changelog

All notable changes to Project XWine are documented in this file.

## [0.2.0] - 2026-11-19 (Phase 1 & Phase 2 Milestone)

### Added
- **Phase 1 Feasibility & Architecture Documentation:**
  - `docs/feasibility-report.md`, `docs/xbox-limitations.md`, `docs/wine-portability.md`, `docs/graphics-compatibility.md`, `docs/architecture.md`, `docs/build-macos.md`, `docs/deployment.md`, `docs/roadmap.md`, plus root docs (`README.md`, `ARCHITECTURE.md`, `BUILD.md`, `TESTING.md`, `LIMITATIONS.md`, `ROADMAP.md`, `CONTRIBUTING.md`).
- **Platform Abstraction Layer (`platform/`):**
  - Thread-safe structured ring-buffered + file-backed `Logger`.
  - Zero-dependency RFC 8259 JSON parser and serializer (`JsonValue`).
  - Schema-validated, atomic `ConfigManager`.
  - Real host frame-time and Mach/POSIX memory/CPU `BenchmarkTracker` + `MacOSPlatform`.
  - Authoritative Xbox Series S Developer Mode hardware profile and UWP capability catalog (`platform/xbox/`).
- **Runtime & Static Inspection (`runtime/`):**
  - Bounds-checked `PeInspector` for PE32/PE32+ DOS/COFF/Optional headers, sections, Shannon entropy, security flags, Import Directory Tables, and honest Xbox blocker classification.
  - `VirtualMemoryBudgetModel` modeling strict UWP W^X (`VirtualAllocFromApp` -> `VirtualProtectFromApp`) and 5,120 MiB / 1,024 MiB caps.
  - `ProcessPolicyController`, `WindowsApiCatalog` (16 core Win32 DLLs), sandboxed `LibraryManager`, and `RuntimeManager` with in-process virtual registry and environment store.
- **Graphics Compatibility Layer (`graphics/`):**
  - Decoupled `GraphicsCapabilityRegistry` covering Direct3D 12, Direct3D 11, Direct3D 10, Direct3D 9 (D3D9On12), Vulkan (no native Xbox ICD), and shader IR translation paths.
- **Controller & Input Subsystem (`input/`):**
  - SDL3 gamepad hotplug manager, radial deadzones, sensitivity curves, trigger thresholds, button remapping presets, keyboard/mouse adapter, and Windows `XInputStateSnapshot` bridge.
- **Steam Metadata Importer (`steam/`):**
  - Valve KeyValues `VdfParser`, `SteamLibraryImporter` (`libraryfolders.vdf` + `appmanifest_*.acf`), and honest `SteamCompatibilityEvaluator`.
- **Native macOS Launcher (`launcher/`):**
  - 1920x1080 logical / Retina 4K HiDPI SDL3 + FreeType + libpng console-style UI across all 7 required navigation views (`HOME`, `LIBRARY`, `STEAM LIBRARY`, `COMPATIBILITY`, `RUNTIMES`, `SETTINGS`, `DIAGNOSTICS`), refined with restrained obsidian-slate craft, calibrated frame-time telemetry chart, and tactile ease-out transitions.
- **Tooling & Automated Verification (`tools/`, `tests/`, `scripts/`):**
  - `xwine-diag` CLI (`sysinfo`, `inspect`, `steam`, `bench`), `Package.appxmanifest.template`, and 4 CTest test suites (`XWineUnitTests`, `XWineIntegrationTests`, `XWineCompatibilityTests`, `XWineLauncherTests`).
