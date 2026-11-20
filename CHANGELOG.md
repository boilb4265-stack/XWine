# Changelog

All notable changes to Project XWine are documented in this file.

## [0.3.0] - 2026-11-19 (Phases 3–5 Complete + Phase 6/7 Xbox UWP Packaging & Probe Kit)

### Added
- **Phase 3 (Runtime Hardening):**
  - `runtime/loader/pe_loader.*`: Full PE32+ Export Table, Base Relocation (`.reloc` DIR64/HIGHLOW), Delay-Load Import, and TLS Directory parsers; `PeImageMapper` with `IVirtualMemoryAllocator` (`XboxUwpMemoryAllocator` using `VirtualAllocFromApp` + `VirtualProtectFromApp` W^X on Windows/Xbox UWP, and safe `DryRunMemoryAllocator` on macOS).
  - `runtime/filesystem/virtual_registry.*`: Thread-safe persistent Windows Virtual Registry (`REG_SZ`, `REG_DWORD`, `REG_QWORD`, `REG_BINARY`, JSON persistence).
  - `runtime/filesystem/path_virtualizer.*`: Sandboxed Windows `C:\` prefix path translator with case-insensitive resolution and traversal/UNC/device blocking.
- **Phase 4 (Minimal Win32-to-OneCoreUAP Shims):**
  - `runtime/windows-api/win32_shims.*`: 25 implemented & tested C ABI Win32 shims across `kernel32.dll`, `user32.dll`, `advapi32.dll`, `xinput1_4.dll`, and `xinput9_1_0.dll`, plus `UnsupportedApiTelemetry` collector.
- **Phase 5 (Graphics Bridge & Standalone Harness):**
  - `graphics/abstraction/swapchain_bridge.*`: `CoreWindowSwapchainBridge` with DXGI `HWND` → `CoreWindow` mapping and 5,120 MiB / 1,024 MiB UMA budget enforcement.
  - `tools/graphics-bench/main.cpp` (`xwine-gfx-test`): Standalone graphics capability & 120-frame SDL3 Metal/hardware pacing test executable independent of the launcher.
- **Phase 6 & 7 (Xbox Series S UWP Packaging Kit & Original Windows Test Probe):**
  - `platform/xbox/src/xbox_uwp_platform.cpp`: Real Windows/Xbox UWP `IPlatform` backend with `GlobalMemoryStatusEx` Game vs App mode detection and `LOCALAPPDATA` sandbox storage.
  - `tools/packaging/Package.appxmanifest` and generated Store/Tile PNG visual assets (`tools/packaging/Assets/*.png`).
  - `scripts/build_xbox_uwp.ps1`, `scripts/deploy_xbox_portal.ps1`, `scripts/prep_xbox_usb.ps1`.
  - `.github/workflows/build-xbox-uwp.yml`: Automated `windows-latest` CI pipeline producing signed `XWine-XboxSeriesS-x64-DevMode.msix` + `.cer` + `xwine-phase7-probe.exe`.
  - `tools/phase7-probe/xwine_probe.c`: Minimal zero-CRT original 64-bit Windows test executable targeting the exact Phase 4 Win32 shim subset.
- **Tests & CLI:**
  - Added `XWineRuntimeShimsTests` and `XWineGraphicsTests` (total: 6 CTest suites / 20 test cases passing).
  - Added `xwine-diag map <file.exe>` and `xwine-diag shims` CLI commands.

## [0.2.0] - 2026-11-19 (Phase 1 & Phase 2 Milestone)

- Initial release with Phase 1 feasibility reports, C++20/SDL3/FreeType launcher (polished with Unslop/Tasteskill/Emil Design-Eng craft pass), static PE32/PE32+ inspector, Steam VDF/ACF importer, SDL3 controller mapping, and 4 CTest suites.
