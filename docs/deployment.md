# Xbox Series S Developer Mode — Deployment Feasibility & Procedure

> **Deployment Status:** **Planned (Phase 6 — Not Yet Tested on Physical Xbox Hardware)**
> **Important:** Project XWine MUST NOT be described as deployed or verified on Xbox until the checklist in Section 6 has been executed on a physical Xbox Series S console.

---

## 1. Why macOS Cannot Build Xbox Packages

Apple Silicon macOS (`arm64-apple-darwin`) has **no supported Microsoft toolchain** to:
- Link against `WindowsApp.lib` / OneCoreUAP UWP umbrella libraries
- Compile C++/WinRT metadata (`cppwinrt.exe`, `midlrt.exe`) targeting `x64` UWP
- Generate valid AppX/MSIX package block maps (`makeappx.exe`)
- Sign packages with a trusted development certificate (`signtool.exe`)

Therefore, XWine uses a **two-stage cross-host workflow**:
1. **Apple Silicon M1 Mac (Daily Development):** Develop, compile, unit-test, and visually verify all shared C++20 libraries (`platform/common`, `runtime/`, `graphics/abstraction`, `input/`, `steam/`, `launcher/`).
2. **Windows 10/11 x64 Host or CI Runner (Xbox Packaging):** Compile the `platform/xbox/` UWP shell + shared C++20 static libraries using MSVC + Windows SDK into a signed `.msix` / `.appx` package for Xbox Developer Mode.

---

## 2. Required Windows SDK & Hardware Prerequisites

### Host PC / Windows VM / CI Runner
- Windows 10 (22H2+) or Windows 11 x64
- Visual Studio 2022 (17.8+) with workloads:
  - **Desktop development with C++**
  - **Universal Windows Platform development** (C++ v143 UWP tools)
- Windows 10/11 SDK (`10.0.22621.0` or newer)
- CMake 3.25+ & Ninja

### Physical Xbox Series S Console
- Xbox Series S enrolled in **Xbox Developer Mode** via Microsoft Partner Center & the **Xbox Dev Mode** activation app.
- Console switched into Dev Mode with **Remote Access (Xbox Device Portal)** enabled.
- Optional External USB 3.1 SSD/HDD formatted as **NTFS** with recursive Windows ACL permissions granted to **`ALL APPLICATION PACKAGES`** (Full Control / Read+Write+Modify).

---

## 3. Required `Package.appxmanifest` Capabilities (Template)

A minimal template is provided at `tools/packaging/Package.appxmanifest.template`:
- Target Device Family: `Windows.Xbox` (`MinVersion="10.0.22621.0"`)
- Processor Architecture: `x64`
- Capabilities:
  - `<Capability Name="internetClient" />` (optional, for local diagnostics/metadata)
  - `<Capability Name="codeGeneration" />` (required ONLY for Phase 7 `VirtualAllocFromApp` / `VirtualProtectFromApp` PE loader experiments in Dev Mode)
  - `<DeviceCapability Name="removableStorage" />` (for ACL-configured NTFS USB game storage)

---

## 4. Packaging & Sideloading Workflow (Windows Host)

1. Build `x64` UWP target with Visual Studio / CMake UWP toolchain (`CMAKE_SYSTEM_NAME=WindowsStore`, `CMAKE_SYSTEM_VERSION=10.0`).
2. Create MSIX/APPX package via Visual Studio **Publish -> Create App Packages -> Sideloading** (or `makeappx.exe pack` + `signtool.exe sign`).
3. Open **Xbox Device Portal** in a browser: `https://<xbox-series-s-ip>:11443`.
4. Under **My games & apps -> Add**, upload the `.msix`/`.appx`, its `.cer` certificate, and the `VCLibs` / `CRT` dependency `.appx` files.
5. **CRITICAL STEP — Switch from App Mode to Game Mode:**
   - Locate **XWine** under **My games & apps**.
   - Open **View details -> App type** and change **`App`** to **`Game`**.
   - Restart the console or relaunch XWine.
   - Verify reported memory ceiling increases from ~1 GB to **~5,120 MB (5 GB)** in XWine's Diagnostics view.

---

## 5. External USB Storage Setup (NTFS ACLs)

Because `broadFileSystemAccess` is ignored on Xbox:
1. Format the USB drive as **NTFS** on Windows.
2. Right-click Drive -> **Properties -> Security -> Edit -> Add...**
3. Enter `ALL APPLICATION PACKAGES`, click **Check Names**, grant **Full Control**, and propagate to all subfolders.
4. Connect the drive to Xbox Series S and select **Media** (not Xbox Games formatting) if prompted.

---

## 6. Phase 6 Hardware Verification Gate (Must All Pass Before Marking Complete)

- [ ] Signed `x64` UWP package installs cleanly via Xbox Device Portal on physical Xbox Series S
- [ ] Package successfully switches to **Game** mode and reports ~5 GB available memory cap
- [ ] Launcher renders at 1920x1080 / 4K at steady 60 FPS
- [ ] Xbox Series S wireless controller navigates all 7 tabs with zero dropped inputs
- [ ] LocalFolder read/write and NTFS USB read verified without `ACCESS_DENIED`
- [ ] Persistent diagnostic logs and hardware benchmark telemetry captured from actual console
