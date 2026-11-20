#pragma once

#include "xwine/runtime/library_manager.hpp"
#include "xwine/steam/steam_compatibility.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xwine::steam {

struct SteamScanReport {
    bool root_found = false;
    std::vector<std::filesystem::path> scanned_libraries;
    std::vector<SteamAppManifest> manifests;
    std::vector<std::string> warnings;
};

class SteamLibraryImporter {
public:
    static std::optional<SteamAppManifest> parse_app_manifest_file(const std::filesystem::path& acf_path,
                                                                   std::string* error_out = nullptr);

    static std::vector<std::filesystem::path> parse_library_folders_vdf(const std::filesystem::path& vdf_path);

    static SteamScanReport scan_steamapps_directory(const std::filesystem::path& steamapps_dir);

    static runtime::GameEntry to_game_entry(const SteamAppManifest& manifest, bool inspect_local_exe = true);
};

} // namespace xwine::steam
