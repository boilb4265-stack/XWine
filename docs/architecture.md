# Project XWine — Software Architecture

> **Status**: Experimental — Phase 1 & Phase 2 Architecture Baseline

---

## 1. Architectural Principles

1. **Strict Platform Separation:** Platform-independent C++20 domain code (`platform/common`, `runtime/`, `graphics/abstraction`, `input/mapping`, `steam/`) contains zero macOS-only or Xbox-only headers. Host-specific details live exclusively under `platform/macos/` and `platform/xbox/`.
2. **Zero False Compatibility:** Every data structure distinguishes between:
   - `Implemented` (code built and verified by automated tests)
   - `ExperimentallyVerified` (proven on physical target hardware)
   - `Planned` (designed interface/roadmap item, not yet running)
   - `TechnicallyUnsupported` (blocked by documented platform/security limits)
3. **Decoupled Graphics & Runtime Layers:** Graphics translation strategies (`graphics/`) are isolated behind `IGraphicsBackend` so future D3D12/D3D11/D3D9On12 experiments are never tightly coupled to Wine or the SDL3 launcher UI.
4. **Safety & Sandboxing First:** The filesystem manager enforces root confinement (no `..` traversal) and mirrors Xbox AppContainer storage constraints (`LocalFolder`, `TemporaryFolder`, ACL-configured removable storage).

---

## 2. Subsystem Topology

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                     XWine Native Launcher (SDL3 + FreeType)             │
│  Views: HOME | LIBRARY | STEAM LIBRARY | COMPATIBILITY | RUNTIMES       │
│         SETTINGS | DIAGNOSTICS  (1920x1080 logical + 4K/HiDPI aware)    │
└──────────┬────────────────────────┬────────────────────────┬────────────┘
           │                        │                        │
           ▼                        ▼                        ▼
┌─────────────────────┐  ┌─────────────────────┐  ┌──────────────────────┐
│   Input Subsystem   │  │   Steam Subsystem   │  │  Graphics Subsystem  │
│  - SDL3 Gamepad     │  │  - VDF/ACF Parser   │  │  - IGraphicsBackend  │
│  - Deadzone/Curves  │  │  - Library Importer │  │  - D3D9/10/11/12     │
│  - Button Remapping │  │  - DRM / Anti-Cheat │  │  - Vulkan Capability │
│  - XInput Snapshot  │  │    Classifier       │  │  - Shader Strategy   │
└──────────┬──────────┘  └──────────┬──────────┘  └──────────┬───────────┘
           │                        │                        │
           └────────────────────────┼────────────────────────┘
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         Runtime Subsystem                               │
│  - PE32/PE32+ Static Metadata & Import Table Inspector (non-executing)  │
│  - Sandboxed Game Library & Storage Manager                             │
│  - Runtime Manager & Virtual Registry / Environment Store               │
│  - Virtual Memory Policy Model (W^X / VirtualAllocFromApp / 5 GB Cap)   │
│  - Process Model (AppContainer Single-Process Policy)                   │
│  - Win32-to-OneCoreUAP API Compatibility Catalog                        │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       Platform Abstraction Layer                        │
│  Common: Logger, JSON, ConfigManager, Benchmark/Telemetry, IPlatform    │
│  macOS (M1/ARM64): Mach memory/CPU telemetry, ~/Library/App Support     │
│  Xbox (Series S UWP): OneCoreUAP capability descriptor & WinRT hooks    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Target Dependency Graph (CMake)

- `xwine_platform` (STATIC): Zero external deps (C++20 STL + macOS Mach/Foundation headers on Apple).
- `xwine_runtime` (STATIC): Links `xwine_platform`.
- `xwine_graphics` (STATIC): Links `xwine_platform`, `xwine_runtime`.
- `xwine_steam` (STATIC): Links `xwine_platform`, `xwine_runtime`.
- `xwine_input` (STATIC): Links `xwine_platform`, `SDL3::SDL3`.
- `xwine_launcher_core` (STATIC): UI state machine, navigation, themes, layout math — testable headlessly without opening a window!
- `xwine-launcher` (EXECUTABLE): SDL3 + FreeType + libpng native GUI binary linking `xwine_launcher_core`, `xwine_input`, `xwine_steam`, `xwine_graphics`, `xwine_runtime`, `xwine_platform`.
- `xwine-diag` (EXECUTABLE): CLI inspection, Steam VDF verification, sysinfo, and micro-benchmark tool.
- `xwine_tests` (EXECUTABLE / CTest): Full unit, integration, compatibility, and launcher state-machine test suites.

---

## 4. Storage Layout

On macOS (`~/Library/Application Support/XWine/`):
- `config.json` — Validated user & runtime settings
- `library.json` — Managed game catalog & cached PE inspection summaries
- `logs/xwine.log` — Structured persistent diagnostic log
- `diagnostics/` — Exported diagnostic JSON reports & benchmarks
- `saves/<game-id>/` — Isolated per-game save directories

On Xbox Dev Mode UWP (`ApplicationData::Current::LocalFolder` + optional NTFS USB `E:\XWine\`):
- Same relative layout, strictly respecting AppContainer folder boundaries and `ALL APPLICATION PACKAGES` NTFS ACL rules.
