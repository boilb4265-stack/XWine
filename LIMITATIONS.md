# Current Limitations & Technical Blockers

Full technical details: [docs/feasibility-report.md](docs/feasibility-report.md), [docs/xbox-limitations.md](docs/xbox-limitations.md), and [docs/wine-portability.md](docs/wine-portability.md).

## Explicit Unsupported / Blocked Areas

1. **No Windows Game Execution Yet:** Project XWine is at Phase 1 & Phase 2 completion. Static PE metadata inspection does **not** execute Windows binaries.
2. **Not Yet Deployed on Physical Xbox Series S Hardware:** Phase 6 requires a Windows 10/11 x64 host with Visual Studio 2022 + Windows SDK (`makeappx.exe` / `signtool.exe`) and a physical Xbox Series S enrolled in Developer Mode.
3. **Unmodified Linux Wine Is Incompatible with Xbox UWP:** Xbox OS has no POSIX kernel, `fork`/`execve`, Unix domain sockets, or `wineserver` multi-process daemon support.
4. **No Native Vulkan Driver on Xbox:** DXVK and VKD3D-Proton cannot run directly on Xbox OS; XWine targets native Xbox Direct3D 12 / Direct3D 11 (`CoreWindow` DXGI shimming) and evaluates Microsoft D3D9On12 for D3D9.
5. **5,120 MiB (5 GB) Dev Mode Memory Ceiling:** Xbox UWP Game Mode caps unified CPU+GPU memory at ~5 GB (and ~1 GB in App Mode), disqualifying memory-heavy modern AAA PC titles.
6. **32-bit x86 (PE32) Blocker:** Xbox UWP processes are 64-bit `x64` without desktop WoW64; 32-bit titles would require software dynamic binary translation.
7. **Storage Sandbox:** `broadFileSystemAccess` is ignored on Xbox. External USB media must be formatted **NTFS** with explicit **`ALL APPLICATION PACKAGES`** Windows ACL permissions.
8. **Steam Client, DRM & Anti-Cheat:** The multi-process CEF Steam client cannot run inside Xbox AppContainer. Kernel anti-cheat drivers (EAC, BattlEye, Vanguard) cannot load in Developer Mode. XWine strictly prohibits bypassing DRM, licensing, authentication, or anti-cheat.
