#include "xwine/runtime/api_catalog.hpp"

#include <algorithm>
#include <cctype>

namespace xwine::runtime {

namespace {
std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

WindowsApiCatalog::WindowsApiCatalog() {
    entries_ = {
        {
            "kernel32.dll", "Core OS",
            Win32DllSupport::NativeOneCoreUAP,
            platform::CapabilityStatus::Planned,
            "Wine maps kernel32 -> ntdll -> Unix/wineserver.",
            "Core threading, heap, sync, and file subset exists in OneCoreUAP; CreateProcess & legacy console APIs blocked."
        },
        {
            "ntdll.dll", "NT Kernel Interface",
            Win32DllSupport::RequiresInProcessShim,
            platform::CapabilityStatus::Planned,
            "Wine ntdll/unix depends on POSIX mmap, signals, and wineserver sockets.",
            "Xbox runs real Windows NT underneath, but direct NT syscalls are unsupported in UWP; wrap permitted OneCoreUAP APIs."
        },
        {
            "user32.dll", "Windowing & Input",
            Win32DllSupport::RequiresInProcessShim,
            platform::CapabilityStatus::Planned,
            "Wine uses winex11/wayland/mac drivers and wineserver queues.",
            "Absent in Xbox UWP. Needs virtual HWND/message queue shim backed by CoreWindow / SDL3 events."
        },
        {
            "gdi32.dll", "2D Graphics",
            Win32DllSupport::RequiresInProcessShim,
            platform::CapabilityStatus::Planned,
            "Wine uses win32u + FreeType + DIB blitting.",
            "Absent in Xbox UWP. Software surface rasterizer uploaded to D3D11/D3D12 texture."
        },
        {
            "d3d12.dll", "Graphics",
            Win32DllSupport::NativeOneCoreUAP,
            platform::CapabilityStatus::Planned,
            "Proton uses VKD3D-Proton (D3D12 -> Vulkan).",
            "Native first-class API in Xbox UWP Game Mode. Pass-through with DXGI CoreWindow swapchain shim."
        },
        {
            "d3d11.dll", "Graphics",
            Win32DllSupport::NativeOneCoreUAP,
            platform::CapabilityStatus::Planned,
            "Proton uses DXVK (D3D11 -> Vulkan).",
            "Native in Xbox UWP Game Mode (via OS D3D11On12). Pass-through with CoreWindow DXGI shim."
        },
        {
            "d3d10.dll", "Graphics",
            Win32DllSupport::RequiresTranslation,
            platform::CapabilityStatus::Planned,
            "Proton uses DXVK.",
            "Thin D3D10-to-D3D11 wrapper over Xbox UWP D3D11."
        },
        {
            "d3d9.dll", "Graphics",
            Win32DllSupport::RequiresTranslation,
            platform::CapabilityStatus::Planned,
            "Proton uses DXVK (D3D9 -> Vulkan).",
            "Absent natively on Xbox UWP; evaluate Microsoft open-source D3D9On12 over native D3D12."
        },
        {
            "vulkan-1.dll", "Graphics",
            Win32DllSupport::TechnicallyUnsupported,
            platform::CapabilityStatus::TechnicallyUnsupported,
            "Native Linux Vulkan ICD.",
            "No Vulkan ICD exists on Xbox OS. Experimental Mesa Dozen (Vulkan-on-D3D12) has high RAM/CPU overhead."
        },
        {
            "opengl32.dll", "Graphics",
            Win32DllSupport::RequiresTranslation,
            platform::CapabilityStatus::Planned,
            "Native Linux OpenGL / Zink.",
            "No native OpenGL driver on Xbox OS; requires ANGLE (GLES->D3D11) or Mesa Gallium D3D12."
        },
        {
            "xinput1_4.dll", "Input",
            Win32DllSupport::RequiresInProcessShim,
            platform::CapabilityStatus::Implemented,
            "Wine maps XInput -> evdev/HID.",
            "XWine input abstraction produces XInput-compatible state snapshots ready for Windows.Gaming.Input / SDL3."
        },
        {
            "xaudio2_9.dll", "Audio",
            Win32DllSupport::NativeOneCoreUAP,
            platform::CapabilityStatus::Planned,
            "Wine uses FAudio over SDL/PulseAudio.",
            "XAudio2 2.9 is natively supported in Xbox UWP."
        },
        {
            "advapi32.dll", "Registry & Security",
            Win32DllSupport::RequiresInProcessShim,
            platform::CapabilityStatus::Planned,
            "Wine routes registry to wineserver hives.",
            "Direct HKLM/HKCU registry writes & Windows Services blocked; redirect to XWine in-process virtual registry."
        },
        {
            "shell32.dll", "Desktop Shell",
            Win32DllSupport::RequiresInProcessShim,
            platform::CapabilityStatus::Planned,
            "Wine maps KnownFolders to prefix drive_c.",
            "Desktop shell absent; map folder queries (AppData, Documents) into sandboxed LocalFolder."
        },
        {
            "ws2_32.dll", "Networking",
            Win32DllSupport::NativeOneCoreUAP,
            platform::CapabilityStatus::Planned,
            "Wine maps Winsock -> BSD sockets.",
            "Winsock2 subset available in UWP with internetClient capability."
        },
        {
            "steam_api64.dll", "Storefront / DRM",
            Win32DllSupport::TechnicallyUnsupported,
            platform::CapabilityStatus::TechnicallyUnsupported,
            "Proton bridges to local Linux Steam client.",
            "Requires live Steam client IPC / DRM. XWine never bypasses DRM or licensing."
        }
    };
}

const Win32DllCompatibilityEntry* WindowsApiCatalog::find_dll(const std::string& dll_name) const {
    const std::string needle = lower_ascii(dll_name);
    for (const auto& entry : entries_) {
        if (lower_ascii(entry.dll_name) == needle) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace xwine::runtime
