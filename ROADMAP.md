# Development Roadmap

See detailed milestone tracking in [docs/roadmap.md](docs/roadmap.md).

- **Phase 1 (Research & Feasibility):** Complete.
- **Phase 2 (Native macOS Launcher & Core Architecture):** Complete.
- **Phase 3 (PE32+ Relocations/IAT Loader, Virtual Registry, Sandboxed Path Virtualizer):** Complete.
- **Phase 4 (Minimal Win32-to-OneCoreUAP Shims & Missing API Telemetry):** Complete (25 tested shims).
- **Phase 5 (Graphics Swapchain Bridge & Independent `xwine-gfx-test`):** Complete (host verified; live D3D12 hardware test pending Phase 6).
- **Phase 6 (Xbox Series S UWP Build, Packaging, Portal Deploy & CI):** Code & automation complete (`build_xbox_uwp.ps1`, `deploy_xbox_portal.ps1`, `prep_xbox_usb.ps1`, `.github/workflows/build-xbox-uwp.yml`); awaiting Windows SDK / CI build + physical Xbox Series S verification.
- **Phase 7 (Original 64-bit DRM-Free Windows Test Probe):** Source & `VirtualAllocFromApp` W^X runner complete (`tools/phase7-probe/xwine_probe.c`); awaiting execution on physical Xbox Series S.
- **Phase 8 (Legitimately Owned DRM-Free Steam Titles):** Metadata importer complete; execution evaluation gated on physical Phase 7 proof.
