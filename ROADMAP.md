# Development Roadmap

See the detailed phased checklist in [docs/roadmap.md](docs/roadmap.md).

- **Phase 1 (Research & Feasibility):** Complete — verified Xbox Series S Dev Mode limits, Wine/Proton incompatibility, graphics translation paths, and macOS/Windows toolchain boundaries.
- **Phase 2 (Native macOS Launcher & Core Architecture):** Complete — buildable C++20/SDL3/FreeType launcher, PE32/PE32+ static inspector, Steam VDF/ACF importer, controller mapping, config/logging/telemetry, and 4 CTest suites passing.
- **Phase 3 (Runtime Foundation Hardening):** In progress — PE relocation/TLS parsing, persistent virtual registry hive, sandboxed Windows path virtualization.
- **Phase 4 (Minimal Win32-to-OneCoreUAP API Shims):** Planned.
- **Phase 5 (Standalone Graphics Micro-Tests — DXGI CoreWindow & D3D9On12):** Planned.
- **Phase 6 (Minimal Xbox Series S UWP Hardware Deployment):** Planned — requires Windows SDK build host and physical Xbox Series S.
- **Phase 7 (Original 64-bit DRM-Free Windows Test Binary Execution):** Planned.
- **Phase 8 (Legitimately Owned DRM-Free Steam Title Evaluation):** Planned.
