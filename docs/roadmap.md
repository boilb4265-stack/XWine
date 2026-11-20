# Project XWine — Development Roadmap

> **Current Stage:** Phase 1 & Phase 2 Complete (macOS Workstation Baseline); Phases 3–8 Progressing Incrementally.

---

## Phase 1: Research & Technical Feasibility — **[COMPLETE]**
- [x] Investigate Xbox Series S physical specs vs. Dev Mode UWP Game/App ceilings (~5 GB / ~1 GB)
- [x] Verify supported graphics APIs (D3D12/D3D11 native; no Vulkan ICD)
- [x] Verify filesystem & storage constraints (`LocalFolder`, NTFS + `ALL APPLICATION PACKAGES` ACLs)
- [x] Analyze Wine PE/Unix architecture (`ntdll/unix` + `wineserver`) and Proton / `steam-runtime`
- [x] Evaluate DXVK / VKD3D-Proton vs. direct D3D12/D3D11 pass-through and D3D9On12
- [x] Document Apple Silicon M1 vs. Windows SDK packaging boundary
- [x] Deliver `docs/feasibility-report.md` and supporting technical reports

## Phase 2: Native macOS Launcher & Core Architecture — **[COMPLETE]**
- [x] Modular C++20 CMake + Ninja project layout (`platform`, `runtime`, `graphics`, `input`, `steam`, `launcher`, `tools`, `tests`)
- [x] Native macOS SDL3 + FreeType 1920x1080 / 4K-aware launcher with all 7 navigation views
- [x] SDL3 Xbox controller abstraction (deadzones, sensitivity curves, remapping, XInput snapshot)
- [x] Sandboxed game library data model & JSON persistence
- [x] Validated configuration manager & structured ring-buffered + file-backed logging
- [x] Real host OS benchmark & frame-time telemetry engine
- [x] Safe, non-executing PE32/PE32+ metadata & import table inspector
- [x] Real Valve VDF / Steam `.acf` metadata importer & DRM/anti-cheat classifier
- [x] Automated unit, integration, compatibility, and launcher UI tests (`ctest`)

## Phase 3: Runtime Foundation Hardening — **[IN PROGRESS]**
- [x] PE32/PE32+ static inspection, import enumeration, section entropy, blocker detection
- [x] Virtual memory W^X / 5 GB budget policy model & Win32-to-OneCoreUAP API catalog
- [ ] Full PE relocation table, delay-load import table, and TLS directory parser
- [ ] Persistent virtual registry hive (`HKLM`/`HKCU` backed by JSON in `LocalFolder`)
- [ ] Deterministic Windows path virtualizer (`C:\` -> sandboxed prefix directory)

## Phase 4: Minimal Win32-to-OneCoreUAP API Surface — **[PLANNED]**
- [ ] Implement isolated, unit-tested shims for minimal `kernel32` subset (heap, file I/O, threads, timing, sync)
- [ ] Implement in-process virtual `user32` window & message pump backed by `CoreWindow` / SDL3
- [ ] Implement minimal `xinput1_4` shim backed by `xwine_input`
- [ ] Publish complete unsupported-API telemetry collector

## Phase 5: Graphics Backend Experiments — **[PLANNED]**
- [ ] Build standalone graphics micro-test harness independent of launcher
- [ ] Prototype DXGI `CreateSwapChainForHwnd` -> `CreateSwapChainForCoreWindow` shim on Windows UWP
- [ ] Evaluate Microsoft D3D9On12 compilation under UWP AppContainer
- [ ] Benchmark memory overhead under simulated 5 GB unified memory ceiling

## Phase 6: Minimal Xbox Series S Deployment — **[PLANNED — BLOCKED ON WINDOWS SDK + PHYSICAL CONSOLE]**
- [ ] Build signed `x64` UWP package on Windows 10/11 host with Visual Studio 2022 + Windows SDK
- [ ] Sideload to physical Xbox Series S in Dev Mode and switch to **Game** mode
- [ ] Verify 60 FPS launch, Xbox controller navigation, `LocalFolder` + NTFS USB storage, and real console telemetry

## Phase 7: First Original Windows Test Application — **[PLANNED]**
- [ ] Author a minimal, original 64-bit (`x86-64`) DRM-free Windows test executable (console + D3D11/D3D12 triangle)
- [ ] Implement in-process `VirtualAllocFromApp` + `VirtualProtectFromApp` PE mapper with IAT thunking
- [ ] Execute on physical Xbox Series S and record memory, CPU, GPU, frame-time, and crash data

## Phase 8: Steam Compatibility Research (DRM-Free Owned Titles First) — **[PLANNED]**
- [ ] Expand Steam metadata importer with cover image ingestion and runtime dependency hints
- [ ] Test legitimately owned, 64-bit, DRM-free PC indie titles after Phase 7 execution is proven
- [ ] Maintain strict policy: never bypass Steam DRM, authentication, licensing, or anti-cheat
