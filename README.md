# Project XWine

> **Status: Experimental — Code-Complete Through Phase 5 + Xbox UWP Packaging Kit & Phase 7 Probe Ready (`v0.3.0`)**
> **Honesty Boundary:** All shared C++20 subsystems, PE32+ relocation/IAT mapper, Phase 4 Win32-to-OneCoreUAP shims, Phase 5 graphics bridge & Metal harness, and all 6 automated test suites are **Implemented & Verified on Apple Silicon macOS**. The Xbox Series S UWP backend, Store assets, PowerShell build/deploy scripts, Phase 7 original Windows x64 probe, and GitHub Actions `windows-latest` MSIX workflow are **Ready to Build & Sideload**, but **on-console execution on a physical Xbox Series S remains Unverified until sideloaded via Windows SDK / CI**.

---

## Building the Signed Xbox Series S `.msix` Package

Apple Silicon macOS cannot natively link `WindowsApp.lib` or run `makeappx.exe` / `signtool.exe`. Choose either supported path to produce the signed Xbox Series S Dev Mode package:

### Option A — Automated GitHub Actions Build (Zero Local Windows PC Needed)
Push this repository to GitHub (or trigger **Actions → Build XWine (macOS ARM64 & Xbox Series S UWP x64)**):
- Workflow: [.github/workflows/build-xbox-uwp.yml](.github/workflows/build-xbox-uwp.yml)
- Runs on `windows-latest` with Visual Studio 2022 + Windows SDK + vcpkg, runs all CTests, compiles `xwine-phase7-probe.exe`, and outputs the downloadable artifact:
  - `XWine-XboxSeriesS-x64-DevMode.msix`
  - `XWine-DevMode-SelfSigned.cer`
  - `xwine-phase7-probe.exe`

### Option B — Local Windows 10/11 x64 PC or VM
```powershell
# 1. Build, test, pack, and sign the Xbox Series S Dev Mode MSIX package
.\scripts\build_xbox_uwp.ps1 -VcpkgRoot C:\vcpkg -Configuration Release

# 2. Sideload directly to your Xbox Series S over LAN via Xbox Device Portal
.\scripts\deploy_xbox_portal.ps1 -XboxIp <XBOX_IP>

# 3. (Optional) Format/ACL-prep an external NTFS USB drive for Xbox UWP access
.\scripts\prep_xbox_usb.ps1 -DriveLetter E
```

**Mandatory On-Console Step:** In Xbox Device Portal (`https://<xbox-ip>:11443`), open **My games & apps → Project XWine → View details** and change **App type** from **`App`** (~1 GB RAM) to **`Game`** (~5,120 MiB RAM + full Direct3D 12/11).

---

## Building & Running Locally on Apple Silicon Mac (M1/M2/M3/M4)

```bash
brew install cmake ninja sdl3 freetype libpng
./scripts/build_macos.sh
./scripts/run_tests.sh

# Launch the native 1920x1080 / Retina HiDPI SDL3 Launcher
./build/launcher/xwine-launcher

# Run standalone Phase 5 graphics benchmark & swapchain tests
./build/tools/graphics-bench/xwine-gfx-test

# Run CLI diagnostics, PE mapping dry-run, and shim listing
./build/tools/diagnostics/xwine-diag sysinfo
./build/tools/diagnostics/xwine-diag shims
./build/tools/diagnostics/xwine-diag map /path/to/program.exe
./build/tools/diagnostics/xwine-diag bench
```

---

## Component Status Matrix

| Phase / Component | Path | Status |
| :--- | :--- | :--- |
| **Phase 1:** Xbox Feasibility, Wine/Proton & Graphics Reports | `docs/` | **Implemented** |
| **Phase 2:** Native C++20 / SDL3 + FreeType Launcher (7 views) | `launcher/` | **Implemented** |
| **Phase 3:** PE32+ Inspector, `.reloc`/Export/TLS/Delay-Import Mapper, Virtual Registry, Sandboxed Path Virtualizer | `runtime/loader/`, `runtime/filesystem/` | **Implemented** |
| **Phase 4:** Minimal Win32-to-OneCoreUAP Shims (`kernel32`, `user32`, `advapi32`, `xinput1_4`) & Missing API Telemetry | `runtime/windows-api/` | **Implemented** |
| **Phase 5:** `CoreWindowSwapchainBridge`, 5 GB UMA Tracker & Independent `xwine-gfx-test` | `graphics/`, `tools/graphics-bench/` | **Implemented** |
| **Phase 6:** Xbox UWP Platform Backend, Manifest, PNG Store Assets, PowerShell Scripts & GitHub Actions MSIX CI | `platform/xbox/`, `tools/packaging/`, `scripts/`, `.github/workflows/` | **Code-Complete (Awaiting Hardware Test)** |
| **Phase 7:** Original 64-bit DRM-Free Windows Test Probe (`xwine-phase7-probe.c`) & `VirtualAllocFromApp` W^X Runner | `tools/phase7-probe/`, `runtime/loader/` | **Code-Complete (Awaiting Hardware Test)** |
| **Phase 8:** Steam VDF/ACF Importer & DRM/Anti-Cheat Classifier | `steam/` | **Implemented (Metadata Only)** |
| Unmodified Linux Wine / Native Vulkan / DXVK / Kernel Anti-Cheat / DRM Bypass | — | **Technically Unsupported** |

---

## Documentation Index

- [docs/feasibility-report.md](docs/feasibility-report.md) — Phase 1 Technical Feasibility Report
- [docs/deployment.md](docs/deployment.md) — Step-by-Step Xbox Series S Dev Mode Build & Sideload Guide
- [docs/xbox-limitations.md](docs/xbox-limitations.md) / [LIMITATIONS.md](LIMITATIONS.md) — Platform Restrictions & Blockers
- [docs/wine-portability.md](docs/wine-portability.md) — Wine & Proton Dependency Audit
- [docs/graphics-compatibility.md](docs/graphics-compatibility.md) — Direct3D / Vulkan / DXVK / D3D9On12 Analysis
- [ARCHITECTURE.md](ARCHITECTURE.md) / [docs/architecture.md](docs/architecture.md) — Software Architecture
- [BUILD.md](BUILD.md) / [docs/build-macos.md](docs/build-macos.md) — Build Instructions
- [TESTING.md](TESTING.md) — Automated Verification Suites (6 CTest suites / 20 tests)
- [ROADMAP.md](ROADMAP.md) / [docs/roadmap.md](docs/roadmap.md) — Development Roadmap
- [CHANGELOG.md](CHANGELOG.md) — Version History

## License

MIT License — see [LICENSE](LICENSE).
