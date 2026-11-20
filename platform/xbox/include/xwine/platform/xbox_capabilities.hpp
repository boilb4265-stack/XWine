#pragma once

#include "xwine/platform/types.hpp"

#include <vector>

namespace xwine::platform {

// Xbox Series S documented hardware and Developer Mode UWP constants.
constexpr uint64_t kXboxSeriesSPhysicalRamBytes = 10ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t kXboxDevModeGameRamCapBytes  = 5120ULL * 1024ULL * 1024ULL; // 5 GiB
constexpr uint64_t kXboxDevModeAppRamCapBytes   = 1024ULL * 1024ULL * 1024ULL; // 1 GiB

struct XboxDevModeRequirement {
    std::string requirement_id;
    std::string title;
    std::string manifest_or_tool;
    bool mandatory_for_phase6 = false;
    bool mandatory_for_phase7 = false;
    std::string notes;
};

std::vector<XboxDevModeRequirement> get_xbox_deployment_requirements();

} // namespace xwine::platform
