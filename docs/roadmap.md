# Project XWine — Development Roadmap

> **Current Stage (`v0.3.0`):** Phases 1–5 implemented and verified on Apple Silicon macOS; Phase 6 Xbox UWP package kit & Phase 7 original Windows x64 probe code-complete and ready for Windows SDK / GitHub Actions MSIX build and physical Xbox Series S hardware verification.

---

## Phase 1: Research & Technical Feasibility — **[COMPLETE]**
- [x] Verified Xbox Series S specs vs. Dev Mode UWP Game (~5 GB) / App (~1 GB) ceilings
- [x] Audited Wine PE/Unix architecture, Proton, DXVK/VKD3D-Proton vs. native D3D12/D3D11/D3D9On12, and NTFS ACL storage
- [x] Delivered `docs/feasibility-report.md` and full technical specification suite

## Phase 2: Native macOS Launcher & Core Architecture — **[COMPLETE]**
- [x] C++20 CMake + Ninja architecture (`platform`, `runtime`, `graphics`, `input`, `steam`, `launcher`, `tools`, `tests`)
- [x] 1920x1080 / 4K-aware SDL3 + FreeType gaming launcher across all 7 tabs with Xbox controller navigation
- [x] Config persistence, structured logging, host telemetry, and Steam VDF/ACF importer

## Phase 3: Runtime Foundation Hardening — **[COMPLETE]**
- [x] Extended PE32+ parser: Export Table, Base Relocations (`.reloc`), Delay-Load Imports, TLS Callbacks
- [x] Full `PeImageMapper` (section mapping, DIR64/HIGHLOW relocation fixups, IAT shim binding, strict W^X protection)
- [x] Persistent thread-safe `VirtualRegistry` (`HKLM`/`HKCU`/`HKCR`, JSON persistence)
- [x] Sandboxed `PathVirtualizer` (`C:\` prefix jail with `..`/UNC/device rejection)

## Phase 4: Minimal Win32-to-OneCoreUAP API Surface — **[COMPLETE — MINIMAL SUBSET]**
- [x] Implemented & unit-tested 25 C ABI shims across `kernel32.dll`, `user32.dll`, `advapi32.dll`, and `xinput1_4.dll`
- [x] Built `UnsupportedApiTelemetry` collector for missing symbol tracking
- [ ] Expand shim coverage iteratively as additional DRM-free Windows test binaries are profiled

## Phase 5: Graphics Bridge & Standalone Verification — **[COMPLETE — CAPABILITY & BRIDGE BASELINE]**
- [x] Implemented `CoreWindowSwapchainBridge` (`HWND` → `CoreWindow` adapter model + 5 GB UMA tracker)
- [x] Built standalone `xwine-gfx-test` executable independent of the launcher (verified 120 Metal frames on M1)
- [ ] On-hardware D3D12/D3D11 live swapchain presentation & D3D9On12 UWP build testing on physical Xbox Series S

## Phase 6: Xbox Series S UWP Packaging & Hardware Verification — **[PACKAGING READY · HARDWARE TEST PENDING]**
- [x] Implemented `platform/xbox/src/xbox_uwp_platform.cpp` + CMake `-DXWINE_TARGET_XBOX_UWP=ON`
- [x] Created `tools/packaging/Package.appxmanifest`, Store/Tile PNG assets, `build_xbox_uwp.ps1`, `deploy_xbox_portal.ps1`, `prep_xbox_usb.ps1`, and `.github/workflows/build-xbox-uwp.yml`
- [ ] Execute GitHub Actions / Windows SDK build to generate signed `XWine-XboxSeriesS-x64-DevMode.msix`
- [ ] Sideload onto physical Xbox Series S, switch **App → Game** mode, verify 5 GB cap, 60 FPS UI, Xbox controller, and storage

## Phase 7: First Original Windows x64 Test Application — **[PROBE CODE READY · HARDWARE TEST PENDING]**
- [x] Authored zero-CRT original 64-bit Windows probe (`tools/phase7-probe/xwine_probe.c`)
- [x] Wired `VirtualAllocFromApp` / `VirtualProtectFromApp` W^X execution runner (`RuntimeManager::run_phase7_probe`)
- [ ] Execute `xwine-phase7-probe.exe` inside XWine on physical Xbox Series S and record hardware telemetry

## Phase 8: Steam Compatibility Research (DRM-Free Owned Titles First) — **[METADATA IMPLEMENTED · EXECUTION PENDING PHASE 7]**
- [x] Steam `libraryfolders.vdf` & `appmanifest_*.acf` importer + DRM/anti-cheat classifier
- [ ] Evaluate legitimately owned, 64-bit, DRM-free Windows indie titles only after Phase 7 succeeds on physical Xbox hardware
