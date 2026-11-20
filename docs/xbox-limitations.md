# Xbox Series S Developer Mode — Technical Limitations & Boundaries

> **Policy Statement:** Project XWine operates strictly within Microsoft's documented Xbox Developer Mode UWP interfaces. XWine never attempts to bypass Xbox security, exploit hypervisor/OS vulnerabilities, modify console firmware, or circumvent DRM or anti-cheat mechanisms.

---

## 1. Platform Environment Summary

Xbox Series S runs a hypervisor-isolated multi-OS architecture:
- **Host Hypervisor:** Enforces hardware partitioning, memory isolation, and code integrity across VMs.
- **System OS (SRA — Shared Resource Allocation):** Runs the Xbox dashboard, networking, system services, and **UWP Apps**.
- **Game OS (ERA — Exclusive Resource Allocation):** Runs retail/GDK `GameCore` titles with ~8 GB RAM and near-bare-metal GPU/CPU scheduling.

Sideloaded **Xbox Developer Mode UWP** packages execute inside a sandboxed **AppContainer** with two resource tiers controlled via Xbox Device Portal:

### Tier A: UWP "App" Mode (Default on Sideload)
- **RAM Limit:** ~1 GB (strict hard cap; OS terminates process upon exceedance).
- **GPU Limit:** ~45% compute allocation; Direct3D 11 Feature Level `10_0`.
- **CPU Limit:** Shared utility cores only.
- **Verdict:** Usable for UI smoke testing only; incapable of hosting modern PC graphics workloads.

### Tier B: UWP "Game" Mode (Explicitly Set in Xbox Device Portal)
- **RAM Limit:** **~5 GB (5,120 MB)** unified CPU + GPU memory budget on both Xbox Series S (10 GB physical) and Series X (16 GB physical).
- **GPU Access:** Full Direct3D 11 and Direct3D 12 feature levels exposed through UWP.
- **CPU Access:** Foreground scheduling on 4 exclusive / up to 6 shared Zen 2 cores (subject to OS version).
- **Verdict:** Mandatory deployment mode for XWine experiments, while still representing a severe memory constraint compared to PC gaming (where 8–16 GB RAM + 4–8 GB VRAM is standard).

---

## 2. Explicit Technical Blockers

### 2.1 Process & Execution Sandbox
- **No `CreateProcess` for Arbitrary Binaries:** Unsigned desktop `.exe` files cannot be spawned as child Win32 processes.
- **Single-Process Boundary:** All XWine components—UI, PE loader, guest code, and shimmed APIs—must coexist inside the single UWP process unless decomposed into valid packaged AppExtensions (which cannot run arbitrary user-provided binaries).
- **Dynamic Code Generation (`codeGeneration`):**
  - Requires `<Capability Name="codeGeneration" />` in `Package.appxmanifest`.
  - Requires `VirtualAllocFromApp` + `VirtualProtectFromApp` with strict `W^X` transitions (`RW` -> `RX`).
  - Retail store certification restricts `codeGeneration`; it is strictly a Developer Mode capability for independent software.

### 2.2 CPU Architecture & 32-bit (x86 / WoW64)
- Xbox Series S uses an **AMD Zen 2 `x86-64`** CPU, allowing native execution of 64-bit x86-64 machine instructions mapped via `VirtualAllocFromApp`.
- However, UWP on Xbox runs **x64 processes only**. Desktop **WoW64** is unavailable to UWP apps; 32-bit (`IMAGE_FILE_MACHINE_I386`) PE binaries cannot jump directly into 32-bit compatibility mode without OS segment-descriptor support (`cs=0x23`), meaning 32-bit Windows games require software dynamic binary translation/interpretation.

### 2.3 Missing Desktop Win32 APIs (OneCoreUAP Subset)
Xbox UWP exposes the **OneCoreUAP / WindowsApp.lib** API partition, omitting core desktop DLLs:
- `user32.dll` (window management, `HWND`, `WndProc`, raw Win32 hooks, GDI cursor/clipboard) — **Absent / Blocked**.
- `gdi32.dll` (GDI device contexts, bitmap blitting, legacy font rendering) — **Absent / Blocked**.
- `shell32.dll` / `ole32.dll` / desktop COM servers — **Mostly Absent**.
- `advapi32.dll` (Windows Services, live `HKLM`/`HKCU` registry writes) — **Blocked** (requires virtualized per-game registry hive).
- `dinput8.dll` / `winmm.dll` joystick APIs — **Absent** (must be translated from `Windows.Gaming.Input` / GameInput / XInput).
- `ntdll.dll` direct syscall stubs — **Unsupported / Unstable** across Xbox OS updates.

### 2.4 Storage & External Media
- `<rescap:Capability Name="broadFileSystemAccess" />` is **ignored on Xbox**.
- Internal storage is restricted to `LocalFolder` and `TemporaryFolder` within the Dev Mode reserved storage quota.
- External USB drives require:
  1. `<DeviceCapability Name="removableStorage" />` in the manifest.
  2. **NTFS filesystem** (FAT32/exFAT fail ACL checks).
  3. Explicit Windows NTFS ACL granting **`ALL APPLICATION PACKAGES`** recursive Read/Write/Modify permissions applied on a PC prior to connecting the drive to Xbox.

### 2.5 Graphics & Windowing Restrictions
- **No Vulkan Driver / ICD** exists on Xbox OS.
- **No Native OpenGL Driver** exists on Xbox OS.
- **DXGI Swapchains** cannot bind to a Win32 `HWND`; they must bind to a UWP `CoreWindow` (`CreateSwapChainForCoreWindow`) or `ISwapChainPanelNative`.
- **SDL3 Dropped WinRT / UWP (`CoreWindow`) Support:** Upstream SDL3 officially removed WinRT/UWP support (`vcpkg` declares `"supports": "!uwp"` for `sdl3`), supporting standard Win32 (`x64-windows`) and official Microsoft GDK instead. Therefore, our automated `windows-latest` CI produces a signed `x64` MSIX package (`XWine-XboxSeriesS-x64-DevMode.msix`) + `xwine-phase7-probe.exe` built against `x64-windows` + `OneCoreUAP`; running the GUI launcher natively inside strict `Windows.Xbox` AppContainer without GDK requires replacing the SDL3 window host with a thin native UWP `CoreWindow` + Direct3D 11/12 host shell (while reusing 100% of `xwine_platform`, `xwine_runtime`, `xwine_graphics`, `xwine_steam`, and `xwine_launcher_core`).

### 2.6 DRM, Storefronts & Anti-Cheat
- **Steam Client (`steam.exe`)**: 32-bit, multi-process Chromium Embedded Framework (CEF), depends on desktop Win32 and Windows Services—cannot execute inside Xbox UWP.
- **SteamStub / Denuvo / Third-Party DRM**: Depends on tamper detection, kernel/OS integrity checks, or live Steam IPC. XWine does not bypass DRM.
- **Kernel Anti-Cheat (Easy Anti-Cheat, BattlEye, Vanguard, Ricochet)**: Requires signed kernel drivers (`*.sys`) and OS-level integrity verification. Completely impossible in Xbox Developer Mode UWP.
