# Project XWine

> **Status: Experimental — Pre-Hardware Verification**
> Project XWine is an experimental research and engineering project investigating whether legitimately owned Windows PC games can execute locally on an **Xbox Series S** in **Xbox Developer Mode** using only supported application interfaces, without a remote PC or cloud streaming.
>
> **Honesty Statement:** No Windows PC game has yet executed on physical Xbox Series S hardware through XWine. The native macOS prototype, PE32/PE32+ static inspector, Steam VDF/ACF metadata importer, SDL3 controller layer, and automated tests are implemented and verified on Apple Silicon macOS. Xbox UWP deployment (Phase 6) and in-process PE execution (Phase 7) remain **Planned**.

---

## Key Technical Findings (Phase 1 Feasibility)

Full analysis: [docs/feasibility-report.md](docs/feasibility-report.md).

1. **Unmodified Linux Wine cannot run on Xbox UWP.** Xbox OS runs a restricted Windows NT kernel (OneCoreUAP), not Linux. Wine's `ntdll/unix` layer and `wineserver` daemon depend on POSIX syscalls, Unix sockets, `fork`/`execve`, and signals unavailable in Xbox AppContainer.
2. **DXVK and VKD3D-Proton are architecturally unsuitable as primary Xbox backends.** Xbox OS provides **native Direct3D 12 and Direct3D 11** (via D3D11On12) in UWP Game Mode and **no Vulkan ICD**. Translating Direct3D to Vulkan on Xbox would require double translation over Mesa Dozen with severe RAM and shader-stutter penalties.
3. **Xbox Series S Developer Mode imposes a ~5,120 MiB (5 GB) RAM ceiling in UWP Game Mode** (and ~1 GB in default App Mode), even though physical hardware contains 10 GB GDDR6.
4. **Legitimate Experimental Path (Phase 7 Hypothesis):** An in-process user-mode `x86-64` (PE32+) loader using the documented UWP `<Capability Name="codeGeneration" />` (`VirtualAllocFromApp` + `VirtualProtectFromApp` W^X transitions), custom Win32-to-OneCoreUAP shims, and native Xbox D3D12/D3D11 (`CoreWindow` swapchain adaptation), targeting original DRM-free 64-bit test binaries first.
5. **macOS Toolchain Constraint:** Apple Silicon macOS cannot natively produce signed `.appx` / `.msix` / `.xvc` Xbox packages. Windows 10/11 x64 + Visual Studio 2022 + Windows SDK are strictly required for Phase 6 hardware packaging.

---

## Implemented Components (Phase 1 & Phase 2)

| Component | Path | Status |
| :--- | :--- | :--- |
| Technical Feasibility & Portability Reports | `docs/` | **Implemented** |
| Native macOS C++20 / SDL3 + FreeType Launcher (1920x1080 / 4K HiDPI) | `launcher/` | **Implemented** |
| PE32 / PE32+ Static Metadata, Section Entropy & Import Table Inspector | `runtime/loader/` | **Implemented** |
| Valve VDF / Steam `.acf` Metadata Importer & DRM/Anti-Cheat Classifier | `steam/` | **Implemented** |
| SDL3 Controller Abstraction (Deadzones, Curves, Remapping, XInput Map) | `input/` | **Implemented** |
| Sandboxed Game Library, Config Manager, Logger & Host Telemetry | `platform/`, `runtime/filesystem/` | **Implemented** |
| Decoupled Graphics Backend & Xbox Route Assessment | `graphics/` | **Implemented** |
| CLI Diagnostic, PE Inspection & Benchmark Tool (`xwine-diag`) | `tools/diagnostics/` | **Implemented** |
| Automated Unit, Integration, Compatibility & Launcher Test Suites | `tests/`, `launcher/tests/` | **Implemented** |
| Xbox Series S UWP Build, Sideload & On-Hardware Verification | `platform/xbox/`, `tools/packaging/` | **Planned (Phase 6)** |
| In-Process x86-64 PE Loader & Win32-to-OneCoreUAP Shims | `runtime/` | **Planned (Phase 4–7)** |

---

## Quick Start (Apple Silicon M1 Mac)

```bash
# Install prerequisites
brew install cmake ninja sdl3 freetype libpng

# Configure & build all libraries, launcher, CLI, and tests
./scripts/build_macos.sh

# Run full automated CTest suite
./scripts/run_tests.sh

# Launch the native SDL3 XWine Launcher
./build/launcher/xwine-launcher

# Run CLI diagnostics and PE inspection
./build/tools/diagnostics/xwine-diag sysinfo
./build/tools/diagnostics/xwine-diag inspect /path/to/windows_binary.exe
./build/tools/diagnostics/xwine-diag bench
```

---

## Documentation Index

- [docs/feasibility-report.md](docs/feasibility-report.md) — Phase 1 Technical Feasibility Report
- [ARCHITECTURE.md](ARCHITECTURE.md) / [docs/architecture.md](docs/architecture.md) — Modular C++20 Architecture
- [BUILD.md](BUILD.md) / [docs/build-macos.md](docs/build-macos.md) — Apple Silicon M1 Build Guide
- [TESTING.md](TESTING.md) — Automated Testing & Verification Strategy
- [LIMITATIONS.md](LIMITATIONS.md) / [docs/xbox-limitations.md](docs/xbox-limitations.md) — Platform Restrictions & Blockers
- [docs/wine-portability.md](docs/wine-portability.md) — Wine & Proton Dependency Audit
- [docs/graphics-compatibility.md](docs/graphics-compatibility.md) — Direct3D / Vulkan / DXVK / D3D9On12 Analysis
- [docs/deployment.md](docs/deployment.md) — Xbox Developer Mode Deployment Requirements
- [ROADMAP.md](ROADMAP.md) / [docs/roadmap.md](docs/roadmap.md) — Phased Engineering Roadmap
- [CHANGELOG.md](CHANGELOG.md) — Release & Engineering History

---

## License

MIT License — see [LICENSE](LICENSE).
