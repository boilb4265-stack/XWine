#pragma once

#include "xwine/platform/types.hpp"

#include <string>
#include <vector>

namespace xwine::runtime {

enum class Win32DllSupport {
    NativeOneCoreUAP,       // Subset available directly via WindowsApp.lib in Xbox UWP
    RequiresInProcessShim,  // Must be shimmed to OneCoreUAP / CoreWindow / Virtual Registry
    RequiresTranslation,    // Needs dedicated mapping layer (e.g., D3D9On12, ANGLE, Mesa)
    TechnicallyUnsupported  // Blocked by Xbox AppContainer / kernel / DRM / anti-cheat rules
};

inline std::string to_string(Win32DllSupport s) {
    switch (s) {
        case Win32DllSupport::NativeOneCoreUAP:      return "OneCoreUAP Subset";
        case Win32DllSupport::RequiresInProcessShim: return "Needs User-Mode Shim";
        case Win32DllSupport::RequiresTranslation:   return "Needs Translation Layer";
        case Win32DllSupport::TechnicallyUnsupported: return "Unsupported in UWP";
    }
    return "Unknown";
}

struct Win32DllCompatibilityEntry {
    std::string dll_name;
    std::string category;
    Win32DllSupport xbox_uwp_support;
    platform::CapabilityStatus xwine_status;
    std::string wine_dependency_note;
    std::string xbox_strategy_note;
};

class WindowsApiCatalog {
public:
    WindowsApiCatalog();

    const std::vector<Win32DllCompatibilityEntry>& entries() const { return entries_; }
    const Win32DllCompatibilityEntry* find_dll(const std::string& dll_name) const;

private:
    std::vector<Win32DllCompatibilityEntry> entries_;
};

} // namespace xwine::runtime
