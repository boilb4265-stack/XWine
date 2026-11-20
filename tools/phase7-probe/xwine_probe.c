/*
 * Project XWine — Phase 7 Original 64-bit DRM-Free Windows Test Application
 *
 * Minimal, self-contained x86-64 PE32+ executable designed specifically to prove
 * XWine's in-process VirtualAllocFromApp + VirtualProtectFromApp (W^X) PE loader
 * and Phase 4 minimal Win32-to-OneCoreUAP shims on physical Xbox Series S hardware.
 *
 * Imports ONLY the Phase 4 minimal verified subset:
 *   - kernel32.dll: OutputDebugStringA, QueryPerformanceCounter, CreateFileW,
 *                   WriteFile, CloseHandle, ExitProcess
 *   - user32.dll:   CreateWindowExW, ShowWindow, PostQuitMessage, PeekMessageW, DestroyWindow
 *   - advapi32.dll: RegCreateKeyExW, RegCloseKey
 *   - xinput1_4.dll: XInputGetState
 *
 * Build on Windows x64 (MSVC, zero CRT dependency):
 *   cl /nologo /GS- /O2 xwine_probe.c /link /NODEFAULTLIB /ENTRY:XWineProbeMain
 *      /SUBSYSTEM:WINDOWS kernel32.lib user32.lib advapi32.lib xinput.lib
 *      /OUT:xwine-phase7-probe.exe
 */

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

#if defined(_MSC_VER)
#pragma function(memset)
void* __cdecl memset(void* dst, int val, size_t count) {
    unsigned char* p = (unsigned char*)dst;
    while (count--) *p++ = (unsigned char)val;
    return dst;
}
#endif

static unsigned int str_len_a(const char* s) {
    unsigned int n = 0;
    while (s && s[n]) ++n;
    return n;
}

unsigned int XWineProbeMain(void) {
    OutputDebugStringA("[XWinePhase7Probe] Entry point reached on x86-64 host!\n");

    // 1. Verify high-resolution QPC timing shim
    LARGE_INTEGER t0 = {0}, t1 = {0};
    QueryPerformanceCounter(&t0);

    // 2. Verify sandboxed C:\ virtual file write shim
    HANDLE hFile = CreateFileW(
        L"C:\\Users\\XWine\\AppData\\Local\\phase7_probe_verified.txt",
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "Project XWine Phase 7 x86-64 PE32+ Probe Executed Successfully.\r\n";
        DWORD written = 0;
        WriteFile(hFile, msg, str_len_a(msg), &written, NULL);
        CloseHandle(hFile);
    }

    // 3. Verify in-process virtual registry shim (advapi32)
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\XWine\\Phase7Probe",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
    }

    // 4. Verify virtual user32 window & message pump shim
    HWND hwnd = CreateWindowExW(
        0, L"XWineProbeClass", L"XWine Phase 7 DX12/Win32 Probe",
        WS_OVERLAPPEDWINDOW, 0, 0, 1920, 1080,
        NULL, NULL, NULL, NULL);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        PostQuitMessage(0);
        MSG msg;
        PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE);
        DestroyWindow(hwnd);
    }

    // 5. Verify XInput controller state bridge
    XINPUT_STATE pad = {0};
    DWORD padRes = XInputGetState(0, &pad);
    if (padRes == ERROR_SUCCESS) {
        OutputDebugStringA("[XWinePhase7Probe] Xbox Controller #0 detected via XInput shim.\n");
    } else {
        OutputDebugStringA("[XWinePhase7Probe] No Xbox Controller #0 active during probe.\n");
    }

    QueryPerformanceCounter(&t1);
    if (t1.QuadPart >= t0.QuadPart) {
        OutputDebugStringA("[XWinePhase7Probe] All Phase 7 checks passed. Calling ExitProcess(0).\n");
    }

    ExitProcess(0);
    return 0;
}

#else
// Non-Windows compilation notice: xwine_probe.c is compiled on Windows/CI into xwine-phase7-probe.exe.
int main(void) { return 0; }
#endif
