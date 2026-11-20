#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xwine::platform {

enum class PlatformId {
    MacOS_Arm64,
    MacOS_X64,
    XboxSeriesS_UWP_GameMode,
    XboxSeriesS_UWP_AppMode,
    Windows_X64,
    Unknown
};

enum class CapabilityStatus {
    Implemented,
    ExperimentallyVerified,
    Planned,
    TechnicallyUnsupported
};

inline std::string to_string(PlatformId id) {
    switch (id) {
        case PlatformId::MacOS_Arm64: return "macOS (Apple Silicon ARM64)";
        case PlatformId::MacOS_X64: return "macOS (x86-64)";
        case PlatformId::XboxSeriesS_UWP_GameMode: return "Xbox Series S (Dev Mode UWP Game)";
        case PlatformId::XboxSeriesS_UWP_AppMode: return "Xbox Series S (Dev Mode UWP App)";
        case PlatformId::Windows_X64: return "Windows (x86-64)";
        case PlatformId::Unknown: return "Unknown";
    }
    return "Unknown";
}

inline std::string to_string(CapabilityStatus status) {
    switch (status) {
        case CapabilityStatus::Implemented: return "Implemented";
        case CapabilityStatus::ExperimentallyVerified: return "Experimentally Verified";
        case CapabilityStatus::Planned: return "Planned";
        case CapabilityStatus::TechnicallyUnsupported: return "Technically Unsupported";
    }
    return "Unknown";
}

struct StorageInfo {
    std::filesystem::path root_path;
    uint64_t total_bytes = 0;
    uint64_t available_bytes = 0;
    uint64_t used_by_xwine_bytes = 0;
    bool writable = false;
    bool sandboxed = true;
    std::string filesystem_notes;
};

struct SystemMetrics {
    uint64_t process_rss_bytes = 0;
    uint64_t total_system_ram_bytes = 0;
    uint64_t target_memory_cap_bytes = 0; // e.g., 5,120 MiB on Xbox Dev Mode Game
    std::optional<double> process_cpu_percent;
    std::optional<double> gpu_utilization_percent; // Only populated when OS provides real telemetry
    uint64_t uptime_ms = 0;
};

struct HardwareProfile {
    std::string device_name;
    std::string cpu_architecture;
    std::string cpu_model;
    uint32_t logical_cores = 0;
    std::string gpu_name;
    uint64_t physical_ram_bytes = 0;
    uint64_t app_usable_ram_bytes = 0;
    std::vector<std::string> supported_graphics_apis;
    std::vector<std::string> unsupported_graphics_apis;
    bool native_x86_64_execution = false;
    bool supports_dynamic_code_gen = false;
    bool hardware_verified = false;
};

struct PlatformCapability {
    std::string name;
    std::string category;
    CapabilityStatus status;
    std::string details;
};

} // namespace xwine::platform
