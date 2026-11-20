#pragma once

#include "xwine/platform/types.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace xwine::platform {

class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual PlatformId platform_id() const = 0;
    virtual HardwareProfile host_hardware() const = 0;
    virtual HardwareProfile target_xbox_series_s_profile(bool game_mode) const = 0;

    virtual std::filesystem::path default_data_directory() const = 0;
    virtual StorageInfo query_storage(const std::filesystem::path& path) const = 0;
    virtual SystemMetrics query_metrics(uint64_t target_cap_bytes) const = 0;

    virtual std::vector<PlatformCapability> capability_matrix() const = 0;
};

std::unique_ptr<IPlatform> create_host_platform();

// Exposed so diagnostics/tests on macOS can inspect the exact documented Xbox Series S
// Dev Mode profile and API matrix without pretending to run on Xbox hardware.
HardwareProfile get_xbox_series_s_hardware_profile(bool game_mode);
std::vector<PlatformCapability> get_xbox_dev_mode_capabilities();

} // namespace xwine::platform
