# Wine & Proton Portability Analysis for Xbox Developer Mode

> **Upstream References Studied:**
> - Wine: `https://gitlab.winehq.org/wine/wine`
> - Proton: `https://github.com/ValveSoftware/Proton`
> - Steam Runtime: `https://github.com/ValveSoftware/steam-runtime`

---

## 1. Executive Conclusion

**Unmodified Wine CANNOT operate as an Xbox Developer Mode UWP application.**

Attempting to compile Wine's Unix runtime layer (`dlls/ntdll/unix/` + `server/wineserver`) for Xbox UWP fails at the fundamental operating-system layer because:
1. Wine translates **Windows NT syscalls -> POSIX/Unix syscalls**.
2. Xbox OS is **already a restricted Windows NT operating system** with **no POSIX userspace**.

---

## 2. Wine Architecture & Dependency Breakdown

Since Wine 9.0's **PE/Unix separation**, Wine divides every system DLL into:
- **PE Side (`dlls/<module>/`)**: Standard Windows PE binaries compiled with MinGW/Clang-PE implementing Win32/NT logic.
- **Unix Side (`dlls/<module>/unix/`)**: Host-native `.so` shared libraries invoked across `__wine_syscall_dispatcher` and `__wine_unix_call` to talk to Linux/macOS kernels.
- **`wineserver` (`server/`)**: A persistent Unix daemon managing handles, cross-process synchronization, virtual registry, window stations, and process/thread tables over Unix domain sockets.

### Module-by-Module Xbox UWP Compatibility Report

| Wine Component | Core Upstream Dependencies | Xbox UWP Compatibility | Exact Blocker / Alternative |
| :--- | :--- | :--- | :--- |
| **`ntdll` (Unix side)** | `mmap`, `mprotect`, `sigaction` (SIGSEGV/SIGTRAP/SIGFPE), `clone`/`pthread`, `futex`/`mach_semaphore`, `syscall` interception, `/proc` | **Incompatible** | No POSIX kernel on Xbox. Must be replaced by a native OneCoreUAP host layer using `VirtualAllocFromApp`, `VirtualProtectFromApp`, and Win32 thread/sync primitives. |
| **`wineserver`** | Separate OS daemon process, `AF_UNIX` sockets, `epoll`/`kqueue`, Unix PID tracking, `SCM_RIGHTS` FD passing | **Incompatible** | UWP AppContainer prohibits spawning background daemons or arbitrary child processes. Synchronization and registry state must be **in-process**. |
| **`kernel32` / `kernelbase`** | Delegates to `ntdll` for files, memory, threads, console, locale, DLL loading (`LdrLoadDll`) | **Partially Reusable (Concept/Spec)** | Many OneCoreUAP `kernelbase` primitives (`CreateFile2`, `CreateThread`, `SRWLock`, `WaitOnAddress`) ALREADY exist natively on Xbox UWP and can be bridged directly instead of emulated through Unix. |
| **`user32`** | `win32u`, X11 (`winex11.drv`) / Wayland (`winewayland.drv`) / Cocoa (`winemac.drv`), `wineserver` window hierarchy & input queues | **Needs Custom UWP Driver** | Desktop display servers do not exist on Xbox. Needs an in-process **virtual window manager** backed by Xbox `CoreWindow` + `Windows.Gaming.Input` / SDL3 events. |
| **`gdi32`** | `win32u`, FreeType, Fontconfig, DIB surface blitting | **Needs Custom Software/D3D Backend** | GDI surfaces can be rasterized in software (or uploaded as D3D11/D3D12 textures) inside XWine's virtual window surface without Unix dependencies. |
| **PE Loader (`dlls/ntdll/loader.c`)** | Maps PE sections, resolves imports, relocations, delayed imports, API sets, TLS callbacks, `DllMain` | **Conceptually Adaptable** | Wine's loader logic is valuable reference material, but actual mapping on Xbox must use `VirtualAllocFromApp` / `VirtualProtectFromApp` and must NOT overwrite the host UWP process's own PEB/LDR lists destructively. |
| **Filesystem (`ntdll/unix/file.c`)** | POSIX `openat`, `stat`, `readdir`, case-folding, symlinks (`dosdevices/c:`) | **Needs OneCoreUAP Rewrite** | Xbox uses NTFS directly; paths must be sandbox-jailed to `LocalFolder` or ACL-configured NTFS USB volumes via `CreateFile2` / `FindFirstFileExFromApp`. |
| **Registry (`server/registry.c`)** | `system.reg` / `user.reg` text hives managed by `wineserver` | **Reusable Pattern (In-Process)** | Implementing an in-process virtual registry backed by JSON/hive files in `LocalFolder` avoids blocked `HKLM`/`HKCU` OS writes cleanly. |
| **Process Creation (`CreateProcessW`)** | `fork`/`execve` launching new `wine64` host processes | **Technically Unsupported** | Xbox AppContainer allows one process per package; child processes cannot be spawned natively. |

---

## 3. Proton & Steam Runtime Dependency Analysis

Valve's **Proton** extends Wine with:
1. **DXVK & VKD3D-Proton**: Translate D3D9/10/11/12 to **Vulkan** (unavailable on Xbox).
2. **`fsync` / `esync` / `ntsync`**: Linux kernel primitives (`eventfd`, `futex_waitv`, `/dev/ntsync`) completely absent on Xbox (and unnecessary on Xbox because native Windows NT synchronization primitives like `CreateEventEx`, `SRWLock`, and `WaitOnAddress` exist in OneCoreUAP!).
3. **`steam-runtime` (Sniper / Soldier)**: Linux container runtime (`pressure-vessel` + `bubblewrap` + Debian/glibc userspace). 100% Linux-specific; non-portable to Xbox OS.
4. **Steamworks Bridge (`lsteamclient.dll` / `vrclient`)**: Communicates with the Linux native Steam client over Unix pipes/shared memory. Cannot function without a running Steam client.

---

## 4. Recommended XWine Runtime Strategy

Rather than fighting a double translation (`Windows Guest -> Wine Unix -> Fake POSIX -> Xbox Windows NT`), XWine adopts a **clean, platform-separated architecture**:

1. **Shared Platform-Independent Core (`runtime/loader`, `runtime/filesystem`, `runtime/windows-api`, `steam/`)**:
   - Portable C++20 PE32/PE32+ static parser, dependency analyzer, API compatibility catalog, sandboxed game library, virtual registry, and Steam VDF/ACF importer.
   - Compiles and tests natively on **Apple Silicon macOS** and **Windows/Xbox**.
2. **macOS Development Host (`platform/macos/`)**:
   - Inspects PE executables, manages library/config/input/UI, runs automated tests, and never falsely claims macOS ARM64 can natively run Windows x86-64 games.
3. **Future Xbox UWP Experimental Runtime (`platform/xbox/`, Phase 6–7)**:
   - Targets **64-bit PE32+ DRM-free binaries** first.
   - Uses an in-process **Win32-to-OneCoreUAP thunk layer** over `VirtualAllocFromApp` / `VirtualProtectFromApp` (`codeGeneration`) and native Xbox Direct3D 11/12.
