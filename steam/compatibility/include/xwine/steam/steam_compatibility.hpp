#pragma once

#include "xwine/runtime/library_manager.hpp"
#include "xwine/runtime/pe_inspector.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xwine::steam {

struct SteamAppManifest {
    uint32_t app_id = 0;
    std::string name;
    std::string install_dir;
    uint64_t size_on_disk_bytes = 0;
    uint32_t state_flags = 0;
    uint64_t last_updated = 0;
    uint64_t build_id = 0;
    std::string library_root;
    std::string manifest_file_path;
};

struct SteamCompatibilityAssessment {
    runtime::CompatibilityRating rating = runtime::CompatibilityRating::Untested;
    bool likely_steam_drm = false;
    bool detected_anti_cheat = false;
    bool exceeds_xbox_5gb_cap_hint = false;
    std::string summary;
    std::vector<std::string> blockers;
};

class SteamCompatibilityEvaluator {
public:
    static SteamCompatibilityAssessment evaluate(
        const SteamAppManifest& manifest,
        const std::optional<runtime::PeInspectionReport>& pe_report);
};

} // namespace xwine::steam
