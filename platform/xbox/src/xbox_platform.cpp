#include "xwine/platform/platform.hpp"
#include "xwine/platform/xbox_capabilities.hpp"

namespace xwine::platform {

HardwareProfile get_xbox_series_s_hardware_profile(bool game_mode) {
    HardwareProfile p;
    p.device_name = game_mode
                        ? "Xbox Series S (Dev Mode — UWP Game Mode)"
                        : "Xbox Series S (Dev Mode — UWP App Mode)";
    p.cpu_architecture = "x86_64 (AMD Zen 2)";
    p.cpu_model = "8x Custom AMD Zen 2 @ 3.6 GHz (3.4 GHz SMT)";
    p.logical_cores = game_mode ? 6 : 4; // Conservative Dev Mode foreground ceiling
    p.gpu_name = "Custom AMD RDNA 2 — 20 CUs @ 1.565 GHz (4.0 TFLOPS)";
    p.physical_ram_bytes = kXboxSeriesSPhysicalRamBytes;
    p.app_usable_ram_bytes = game_mode ? kXboxDevModeGameRamCapBytes : kXboxDevModeAppRamCapBytes;
    p.native_x86_64_execution = true;
    p.supports_dynamic_code_gen = true; // Requires <Capability Name="codeGeneration"/> + VirtualAllocFromApp
    p.hardware_verified = false;        // Must remain false until verified on physical Xbox Series S hardware!

    if (game_mode) {
        p.supported_graphics_apis = {
            "Direct3D 12 (Native UWP Game Mode)",
            "Direct3D 11 (Native via D3D11On12)"
        };
    } else {
        p.supported_graphics_apis = {
            "Direct3D 11 Feature Level 10_0 (Restricted ~45% GPU)"
        };
    }

    p.unsupported_graphics_apis = {
        "Native Vulkan ICD (Not provided by Xbox OS)",
        "Direct DXVK / VKD3D-Proton (Requires Vulkan ICD)",
        "Native Desktop OpenGL",
        "Native Direct3D 9 (Requires D3D9On12)"
    };
    return p;
}

std::vector<PlatformCapability> get_xbox_dev_mode_capabilities() {
    return {
        {
            "Native macOS C++20 / SDL3 Launcher",
            "Launcher",
            CapabilityStatus::Implemented,
            "1920x1080 / 4K-aware dark gaming UI with Xbox controller navigation, search, settings, and live diagnostics."
        },
        {
            "PE32 / PE32+ Static Metadata & Import Inspector",
            "Runtime",
            CapabilityStatus::Implemented,
            "Parses MS-DOS, COFF, Optional headers, sections, and Import Directory Tables without executing untrusted binaries."
        },
        {
            "Steam VDF / AppManifest Metadata Importer",
            "Steam",
            CapabilityStatus::Implemented,
            "Imports legitimately owned game metadata from libraryfolders.vdf and appmanifest_*.acf; never bypasses DRM."
        },
        {
            "SDL3 Controller & XInput Mapping Layer",
            "Input",
            CapabilityStatus::Implemented,
            "Supports Xbox controller hotplug, radial deadzones, sensitivity curves, trigger thresholds, and button remapping."
        },
        {
            "Xbox Series S UWP Packaging & Launch (Phase 6)",
            "Platform",
            CapabilityStatus::Planned,
            "Requires Windows 10/11 x64 + Visual Studio 2022 + Windows SDK (makeappx/signtool) and physical Xbox hardware."
        },
        {
            "In-Process x86-64 PE Loader via VirtualAllocFromApp (Phase 7)",
            "Runtime",
            CapabilityStatus::Planned,
            "Uses UWP codeGeneration capability (W^X RW->RX) and Win32-to-OneCoreUAP thunks; unverified on console hardware."
        },
        {
            "Direct3D 12 / 11 CoreWindow Swapchain Shim",
            "Graphics",
            CapabilityStatus::Planned,
            "Targets native Xbox UWP D3D12/D3D11 directly instead of translating through Vulkan."
        },
        {
            "Direct3D 9 via Microsoft D3D9On12",
            "Graphics",
            CapabilityStatus::Planned,
            "Evaluates Microsoft's open-source D3D9On12 mapping layer over native Xbox D3D12."
        },
        {
            "Unmodified Linux Wine (ntdll/unix + wineserver)",
            "Runtime",
            CapabilityStatus::TechnicallyUnsupported,
            "Requires POSIX syscalls, fork/execve, signals, and Unix domain sockets unavailable in Xbox UWP AppContainer."
        },
        {
            "Native Vulkan / Direct DXVK / VKD3D-Proton",
            "Graphics",
            CapabilityStatus::TechnicallyUnsupported,
            "Xbox OS provides no Vulkan ICD; double translation (DXVK -> Dozen -> D3D12) exceeds 5 GB RAM and CPU budgets."
        },
        {
            "32-bit x86 (PE32) Native Execution without Software JIT",
            "Runtime",
            CapabilityStatus::TechnicallyUnsupported,
            "Xbox UWP runs 64-bit x64 processes without desktop WoW64; 32-bit binaries require software dynamic translation."
        },
        {
            "Steam Client, SteamStub DRM, & Kernel Anti-Cheat",
            "Steam",
            CapabilityStatus::TechnicallyUnsupported,
            "Multi-process CEF Steam client cannot run in AppContainer; EAC/BattlEye require kernel drivers; DRM bypass is forbidden."
        }
    };
}

std::vector<XboxDevModeRequirement> get_xbox_deployment_requirements() {
    return {
        {
            "WIN_SDK",
            "Windows 10/11 x64 Build Host + Visual Studio 2022 UWP C++ Workload",
            "Windows SDK 10.0.22621.0+, makeappx.exe, signtool.exe",
            true,
            true,
            "Required because macOS has no supported Microsoft Xbox UWP/GDK packaging or signing toolchain."
        },
        {
            "GAME_MODE",
            "Reclassify Sideloaded Package from App Mode to Game Mode",
            "Xbox Device Portal (https://<xbox-ip>:11443) -> View Details -> Game",
            true,
            true,
            "Raises Dev Mode memory ceiling from ~1 GB to ~5,120 MB and enables full D3D11/D3D12 feature levels."
        },
        {
            "USB_ACL",
            "NTFS External USB Drive with ALL APPLICATION PACKAGES ACL",
            "<DeviceCapability Name=\"removableStorage\" /> + Windows NTFS Security ACL",
            false,
            true,
            "broadFileSystemAccess is ignored on Xbox; FAT32/exFAT drives fail Win32 CreateFile2 access checks."
        },
        {
            "CODEGEN",
            "Dynamic Code Generation Manifest Capability",
            "<Capability Name=\"codeGeneration\" /> + VirtualAllocFromApp / VirtualProtectFromApp",
            false,
            true,
            "Mandatory for Phase 7 W^X (RW->RX) mapping of x86-64 guest PE code pages in Dev Mode."
        }
    };
}

} // namespace xwine::platform
