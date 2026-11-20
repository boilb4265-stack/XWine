# Project XWine — Phase 1 Technical Feasibility Report

> **Project Status**: **Experimental — Pre-Hardware Verification**
> **Target Console**: Xbox Series S (Xbox Developer Mode)
> **Development Workstation**: Apple Silicon MacBook (M1 / ARM64 macOS)
> **Core Question**: Can legitimately owned Windows PC games execute locally on an Xbox Series S in Xbox Developer Mode using only supported application interfaces, without a remote PC or cloud streaming?

---

## 1. Executive Verdict

**Short Answer:**
- **Porting unmodified Linux Wine + DXVK + VKD3D-Proton to Xbox Developer Mode is TECHNICALLY UNSUPPORTED and architecturally inverted.** Xbox OS is a locked-down Windows NT kernel (OneCoreUAP / GameCore), not Linux; it provides neither POSIX syscalls nor a native Vulkan ICD.
- **Executing arbitrary commercial Steam PC games on Xbox Series S in Developer Mode is NOT currently feasible** due to single-process AppContainer sandboxing, the ~5 GB UWP Game Mode RAM cap, absence of WoW64 for 32-bit x86 titles, missing desktop Win32 subsystems (`user32`, `gdi32`, full `shell32`, unrestricted registry), Steam client multi-process/CEF dependencies, DRM (SteamStub/Denuvo), and kernel-mode anti-cheat (EAC, BattlEye, Vanguard).
- **Executing small, legitimately owned, DRM-free 64-bit (`x86-64`) Windows PE applications locally inside an Xbox Developer Mode UWP "Game" package IS ARCHITECTURALLY PLAUSIBLE (Planned / Unverified on Hardware)** via a **custom in-process user-mode PE loader and Win32-to-OneCoreUAP API thunking layer** backed by:
  1. Native AMD Zen 2 `x86-64` CPU execution (no ISA emulation required for PE32+ code).
  2. The documented UWP `<Capability Name="codeGeneration" />` manifest capability + `VirtualAllocFromApp` / `VirtualProtectFromApp` W^X memory management.
  3. Native Xbox UWP **Direct3D 12** and **Direct3D 11** (backed by D3D11On12) with `CoreWindow` swapchain adaptation—bypassing Vulkan/DXVK/VKD3D-Proton entirely for D3D11/D3D12 titles, and evaluating Microsoft's open-source **D3D9On12** for D3D9.

**Honesty Guarantee:** No Windows game has yet been executed on physical Xbox Series S hardware by XWine. Until a binary executes on physical hardware and passes Phase 6 & Phase 7 benchmarks, all runtime execution claims remain strictly **Planned / Unverified**.

---

## 2. Capability Classification Matrix

Per Project XWine engineering standards, every subsystem is explicitly classified:

| Subsystem | Status | Technical Reality |
| :--- | :--- | :--- |
| Native macOS C++20/SDL3 Launcher UI | **Implemented** | Builds and runs natively on Apple Silicon M1 (`arm64-apple-darwin`). |
| PE32 / PE32+ Static Metadata & Import Inspector | **Implemented** | Parses DOS/COFF/Optional headers, sections, imports, graphics/DRM markers without executing code. |
| Valve VDF / Steam `.acf` Metadata Importer | **Implemented** | Parses `libraryfolders.vdf` & `appmanifest_*.acf` for owned game metadata only. |
| SDL3 Controller Abstraction & XInput Mapping | **Implemented** | Deadzones, sensitivity curves, remapping, hotplug via SDL3 Gamepad API. |
| Sandboxed Library, Config, Logging & Benchmarks | **Implemented** | JSON persistence, structured ring-buffer logs, real host OS memory/CPU/frame-time telemetry. |
| Xbox UWP Packaging & On-Hardware Launch (Phase 6) | **Planned** | Requires Windows 10/11 x64 host + Visual Studio 2022 + Windows SDK (`makeappx.exe`, `signtool.exe`) + physical Xbox Series S in Dev Mode. |
| In-Process x86-64 PE Loader via `VirtualAllocFromApp` (Phase 7) | **Planned** | Relies on UWP `codeGeneration` capability + custom Win32-to-OneCoreUAP thunking; not yet verified on console hardware. |
| D3D12 / D3D11 Swapchain Shimming (`HWND` -> `CoreWindow`) | **Planned** | Feasible on Xbox UWP Game Mode because D3D12/D3D11 are native to Xbox OS. |
| D3D9 Translation via D3D9On12 | **Planned** | Open-source Microsoft mapping layer; requires UWP-compatible build and testing. |
| Unmodified Linux Wine (`ntdll/unix` + `wineserver`) | **Technically Unsupported** | Depends on POSIX syscalls, Unix sockets, `fork`/`execve`, signals, and multi-process IPC absent in Xbox UWP. |
| Native Vulkan Driver / Direct DXVK / VKD3D-Proton | **Technically Unsupported** | Xbox OS ships no Vulkan ICD. Stacking DXVK over Mesa Dozen over D3D12 adds severe overhead under the 5 GB RAM cap. |
| 32-bit x86 (PE32) Native Execution without JIT | **Technically Unsupported** | Xbox UWP runs 64-bit x64 processes without desktop WoW64 subsystem; requires a software x86->x64 dynamic translator or interpreter. |
| Steam Client (`steam.exe`) Execution in UWP | **Technically Unsupported** | 32-bit, multi-process Chromium/CEF architecture, desktop Win32 hooks, and background Steam Service are disallowed in AppContainer. |
| Steam DRM Bypass / Kernel Anti-Cheat (EAC, BattlEye) | **Technically Unsupported** | Kernel drivers cannot load in Dev Mode UWP; DRM/anti-cheat bypass is strictly out of scope and prohibited. |
| Native macOS `.appx` / `.msix` / `.xvc` Xbox Packaging | **Technically Unsupported** | Microsoft provides no macOS Xbox UWP/GDK SDK or signing toolchain. |

---

## 3. Xbox Series S Hardware vs. Developer Mode Allocation

A critical mistake in console research is assuming physical hardware specifications equal usable application budgets.

### 3.1 Physical Hardware (Xbox Series S)
- **CPU:** Custom AMD Zen 2, 8 cores / 16 threads @ 3.6 GHz (3.4 GHz with SMT enabled), `x86-64` ISA.
- **GPU:** Custom AMD RDNA 2, 20 Compute Units (CUs) @ 1.565 GHz (~4.0 TFLOPS FP32).
- **Memory:** 10 GB GDDR6 unified memory across a 128-bit bus with split bandwidth:
  - 8 GB @ 224 GB/s (fast pool)
  - 2 GB @ 56 GB/s (slow pool, OS/system)
- **Storage:** 512 GB or 1 TB custom NVMe SSD (+ USB 3.1 external storage).

### 3.2 Developer Mode UWP Resource Ceilings
In retail/commercial GDK (`GameCore` / `.xvc`) titles, Series S games receive ~8 GB RAM and bare-metal access to 7 CPU cores and full RDNA 2 features. **Xbox Developer Mode (independent UWP sideloading) does NOT grant GDK GameCore budgets:**

| UWP Classification | Usable RAM Cap | CPU / GPU Allocation | Graphics Capability | Viability for XWine |
| :--- | :--- | :--- | :--- | :--- |
| **UWP "App" Mode** (Default sideload) | **~1.0 GB** (strict termination on overrun) | Shared background threads; ~45% GPU cap | D3D11 Feature Level 10_0 | **Insufficient** — launcher only; cannot host PC games |
| **UWP "Game" Mode** (Reclassified in Dev Portal) | **~5.0 GB (5,120 MB)** uniform cap across Series S & X | Dedicated foreground cores (4 exclusive / 6 shared depending on OS rev); full GPU access | Full Direct3D 11 & Direct3D 12 feature levels | **Minimum viable experimental target** |
| **GDK / GameCore** (`.xvc` / `.msixvc`) | **~8.0 GB** on Series S | Near-bare-metal Zen 2 + RDNA 2 Velocity Architecture | DirectX 12 Ultimate | Requires approved ID@Xbox / Microsoft Partner Program; unavailable for informal open-source Dev Mode experimentation |

**Implication:** Even in UWP Game Mode, XWine shares a **5,120 MB unified CPU+GPU memory pool** for the launcher, runtime tables, guest PE images, guest heap, GPU textures, command buffers, and shader caches. Any AAA Windows game requiring >4 GB combined RAM+VRAM will hit OOM termination immediately.

---

## 4. Application Execution & Security Restrictions

Xbox Developer Mode runs sideloaded UWP packages inside a Windows **AppContainer** sandbox:

1. **No Arbitrary Process Creation (`CreateProcess`):** A UWP app cannot spawn arbitrary unsigned `.exe` binaries from disk as child OS processes. Multi-process Windows launchers and games that spawn child worker/updater/DRM executables cannot operate using OS processes.
2. **Code Integrity & Dynamic Code Generation (`W^X`):**
   - Standard desktop `VirtualAlloc(..., PAGE_EXECUTE_READWRITE)` is rejected by AppContainer policy.
   - Legitimate dynamic code execution (used by emulators/JITs in Dev Mode) requires declaring `<Capability Name="codeGeneration" />` in `Package.appxmanifest` and calling **`VirtualAllocFromApp`** (`PAGE_READWRITE`) followed by **`VirtualProtectFromApp`** (`PAGE_EXECUTE_READ`).
   - This enables an **in-process user-mode PE loader** to map 64-bit PE sections, relocate code, patch IATs to XWine thunks, flip `.text` to RX, and branch into the entry point—within the single permitted UWP process.
3. **Structured Exception Handling (SEH) & TLS:** x64 Windows binaries rely on `.pdata` unwind tables (`RtlAddFunctionTable`) and thread-local storage (`gs:[0x58]` TEB/TLS array). Manipulating the host UWP process's TEB/PEB directly risks corrupting the host runtime; guest TLS and module lists must be carefully isolated or cooperatively managed.
4. **32-bit x86 (PE32) Blocker:** Many older indie/DRM-free Windows games are 32-bit (`IMAGE_FILE_MACHINE_I386`). Xbox UWP runs strictly as an `x64` process without desktop WoW64 transitions. Executing 32-bit code inside a 64-bit UWP process requires a software x86-to-x64 dynamic translator/interpreter—making **64-bit (`x86-64`) PE32+ binaries the mandatory Phase 7 starting target**.

---

## 5. Filesystem & Storage Restrictions

- `broadFileSystemAccess` is **unsupported / ignored on Xbox**.
- Desktop paths such as `C:\Program Files (x86)\Steam` do not exist and cannot be accessed.
- Supported storage scopes in Xbox Dev Mode UWP:
  1. **App Local Storage (`ApplicationData::Current::LocalFolder`)**: Fast internal SSD storage scoped exclusively to the package, capped by the Developer Mode storage partition allocation configured in Dev Home.
  2. **App Temporary Storage (`TemporaryFolder`)**: Suitable for transient shader caches and decompressed diagnostics.
  3. **External USB Storage (Removable Media)**: Requires `<DeviceCapability Name="removableStorage" />` AND the USB drive must be formatted **NTFS** on a PC with explicit recursive Windows ACL permissions granted to **`ALL APPLICATION PACKAGES`** (Read/Write/Modify). FAT32 and exFAT do not store NTFS security descriptors and fail Win32 `CreateFile2` writes under AppContainer.

---

## 6. Wine & Proton Dependency Analysis (Summary)

Full breakdown: see [wine-portability.md](./wine-portability.md).

- **Wine (since Wine 9.0 PE/Unix separation)** splits every DLL into a pure PE component (`dlls/<name>/`) and a native Unix `.so` host bridge (`dlls/<name>/unix/`), coordinated by a separate **`wineserver`** daemon over Unix domain sockets.
- **Why Wine cannot be compiled directly for Xbox UWP:**
  - `dlls/ntdll/unix/` requires POSIX `mmap`, `mprotect`, `sigaction` (SIGSEGV/SIGTRAP exception synthesis), `clone`/`pthread`, `futex`/Mach semaphores, `socketpair`, `fork`/`execve`, and `/proc`. None exist in Xbox UWP.
  - `wineserver` requires a persistent multi-process daemon communicating via Unix sockets and tracking Unix PIDs. Xbox UWP forbids spawning background daemons.
- **What CAN be reused intellectually and selectively from Wine:**
  - Wine's clean-room **user-space DLL specifications, data structure definitions, and high-level Win32 semantics** (e.g., virtual registry semantics, path canonicalization, PE loader edge cases, `user32`/`gdi32` message queue state machine, DirectInput/XInput translation).
  - Instead of translating `Win32 -> NT -> Unix`, XWine's Xbox architecture must translate `Desktop Win32 -> XWine User-Mode Shim -> OneCoreUAP / WinRT / Native D3D12`.
- **Proton & Steam Runtime:**
  - Valve's Proton bundles Wine + DXVK + VKD3D-Proton + `steam-runtime` (a Linux container environment built on `pressure-vessel`, bubblewrap, glibc, and Linux X11/Wayland/Vulkan libraries). None of `steam-runtime`'s Linux container infrastructure can run on Xbox OS.

---

## 7. Graphics Translation Feasibility (Summary)

Full breakdown: see [graphics-compatibility.md](./graphics-compatibility.md).

- **Critical Insight:** Xbox OS natively provides **Direct3D 12** and **Direct3D 11** (via D3D11On12) directly in UWP Game Mode. It provides **zero native Vulkan support**.
- **DXVK (D3D9/10/11 -> Vulkan)** and **VKD3D-Proton (D3D12 -> Vulkan)** exist to run Direct3D games on *Linux/Vulkan*. Using them on Xbox would require routing `D3D -> Vulkan -> Mesa Dozen (Vulkan-on-D3D12) -> Xbox D3D12`, adding massive CPU, RAM, and shader-stutter penalties.
- **Recommended Graphics Strategy for XWine:**
  - **D3D12 Guest:** Direct API pass-through to Xbox UWP D3D12, shimming only desktop `IDXGIFactory::CreateSwapChainForHwnd` -> `CreateSwapChainForCoreWindow` and desktop display enumeration.
  - **D3D11 / D3D10 Guest:** Direct pass-through to Xbox UWP D3D11 (native D3D11On12) with `HWND` -> `CoreWindow` DXGI swapchain translation.
  - **D3D9 Guest:** Integrate or adapt Microsoft's open-source **D3D9On12** targeting native D3D12.
  - **OpenGL / Vulkan Guest:** Long-term experimental evaluation of **ANGLE** (OpenGL ES -> D3D11) and **Mesa Gallium D3D12 / Dozen** compiled for UWP, acknowledging heavy memory overhead.

---

## 8. Required SDKs & Apple Silicon M1 Workstation Constraints

- **Development Workstation (M1 Mac, `arm64-apple-darwin`):**
  - Supports **all platform-independent C++20 development**: Launcher UI (SDL3 + FreeType), PE metadata inspector, Steam VDF/ACF importer, controller abstraction, virtual memory/API catalogs, config, logging, benchmarking, and automated unit/integration tests.
  - Uses `CMake`, `Ninja`, `Apple Clang` (C++20), `Git`, `SDL3`, `FreeType`, `libpng`, and `Python 3`.
- **Xbox UWP Compilation & Packaging (Mandatory Windows Dependency):**
  - There is **no officially supported macOS SDK** capable of compiling Xbox UWP C++/WinRT targets, generating signed `.appx` / `.msix` packages (`makeappx.exe`, `signtool.exe`), or linking against `WindowsApp.lib` / OneCoreUAP headers.
  - Phase 6 (Xbox Deployment) strictly requires a **Windows 10/11 x64 host** (physical PC, Windows 11 ARM64/x64 VM, or GitHub Actions `windows-latest` runner) with:
    1. Visual Studio 2022 (Desktop C++ + Universal Windows Platform C++ workload)
    2. Windows 10/11 SDK (10.0.22621.0 or newer)
    3. Xbox Developer Mode Activation app on physical Xbox Series S
    4. Xbox Device Portal (`https://<console-ip>:11443`) for sideloading and switching the package from **App** mode to **Game** mode.

---

## 9. Go / No-Go Decision & Phased Path Forward

| Decision | Verdict |
| :--- | :--- |
| Proceed with Phase 2 (Native macOS C++20/SDL3 Launcher, PE Inspector, Steam Metadata Importer, Controller Input, Tests)? | **GO** — Fully buildable and testable on M1 Mac right now. |
| Attempt direct port of Linux Wine + DXVK to Xbox UWP? | **NO-GO** — Fundamentally incompatible with Xbox OS / AppContainer. |
| Proceed toward Phase 3–7 via custom in-process x86-64 Win32-on-OneCoreUAP architecture targeting original DRM-free 64-bit test executables? | **CONDITIONAL GO** — Scientifically sound hypothesis; must be proven incrementally on physical Xbox Series S hardware in Phase 6 & 7 before claiming any game compatibility. |
