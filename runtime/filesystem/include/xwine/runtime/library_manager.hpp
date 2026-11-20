#pragma once

#include "xwine/runtime/pe_inspector.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xwine::runtime {

// Deliberately honest ratings: XWine NEVER assigns VerifiedXbox automatically.
enum class CompatibilityRating {
    Untested,
    InspectOnly,
    Blocked_32Bit,
    Blocked_DRM,
    Blocked_AntiCheat,
    Blocked_MissingRuntime,
    Incompatible,
    Experimental_HostOnly,
    Verified_Xbox // Reserved ONLY for Phase 6/7 physical hardware verification
};

inline std::string to_string(CompatibilityRating r) {
    switch (r) {
        case CompatibilityRating::Untested:               return "Untested";
        case CompatibilityRating::InspectOnly:            return "Inspect Only";
        case CompatibilityRating::Blocked_32Bit:          return "Blocked (32-bit)";
        case CompatibilityRating::Blocked_DRM:            return "Blocked (DRM)";
        case CompatibilityRating::Blocked_AntiCheat:      return "Blocked (Anti-Cheat)";
        case CompatibilityRating::Blocked_MissingRuntime: return "Blocked (No Runtime)";
        case CompatibilityRating::Incompatible:           return "Incompatible";
        case CompatibilityRating::Experimental_HostOnly:  return "Experimental (Host)";
        case CompatibilityRating::Verified_Xbox:          return "Verified (Xbox)";
    }
    return "Untested";
}

CompatibilityRating parse_compatibility_rating(const std::string& text);

struct LaunchOptions {
    std::string arguments;
    std::string runtime_profile = "InspectOnly";
    std::string graphics_backend = "Auto";
    std::string controller_profile = "XboxStandard";
    bool enable_logging = true;
    std::map<std::string, std::string> env_vars;
};

struct GameEntry {
    std::string id;
    std::string title;
    std::string developer;
    std::string publisher;
    std::string description;
    std::string genre;
    std::string source; // "Manual", "SteamMetadata", "SampleFixture"
    uint32_t steam_app_id = 0;

    std::string install_directory;
    std::string executable_relative_path;
    std::string cover_image_path;
    uint32_t accent_color_rgb = 0x0EA5E9; // Procedural card accent fallback

    uint64_t size_on_disk_bytes = 0;
    bool installed = false;
    bool favorite = false;
    bool drm_free_declared = false;

    uint64_t added_epoch_sec = 0;
    uint64_t last_played_epoch_sec = 0;
    uint32_t play_count = 0;

    CompatibilityRating compatibility = CompatibilityRating::Untested;
    std::string compatibility_summary;
    std::vector<std::string> blockers;

    // Cached static PE inspection metadata
    bool pe_inspected = false;
    std::string pe_architecture;
    std::string pe_format;
    std::string pe_subsystem;
    std::string pe_graphics_api;
    uint32_t pe_import_count = 0;
    std::vector<std::string> pe_imported_dlls;
    std::string executable_checksum_fnv1a;

    LaunchOptions launch_options;
};

enum class LibraryFilter {
    All,
    Installed,
    Favorites,
    RecentlyPlayed,
    Inspected,
    DrmFree
};

enum class LibrarySort {
    TitleAsc,
    RecentlyAddedDesc,
    RecentlyPlayedDesc,
    SizeDesc
};

class LibraryManager {
public:
    LibraryManager() = default;

    void set_sandbox_root(const std::filesystem::path& root);
    const std::filesystem::path& sandbox_root() const { return sandbox_root_; }

    // Security check: verifies target path stays inside root (no ".." escape).
    static bool is_path_within_root(const std::filesystem::path& root,
                                    const std::filesystem::path& candidate);

    // Deterministic FNV-1a 64-bit file checksum for safe integrity validation.
    static std::optional<std::string> compute_file_fnv1a_hex(const std::filesystem::path& path);

    bool add_or_update_game(const GameEntry& entry);
    bool remove_game(const std::string& id);
    GameEntry* find_game(const std::string& id);
    const GameEntry* find_game(const std::string& id) const;

    const std::vector<GameEntry>& all_games() const { return games_; }
    std::vector<const GameEntry*> query(LibraryFilter filter, LibrarySort sort,
                                        const std::string& search_query = "") const;

    bool toggle_favorite(const std::string& id);
    void record_inspection_event(const std::string& id);

    // Runs safe static PE inspection on a game's executable and updates honest blockers.
    PeInspectionReport inspect_game_executable(const std::string& id);

    // Discovers .exe files inside a game directory safely.
    static std::vector<std::filesystem::path> discover_executables(const std::filesystem::path& dir,
                                                                   size_t max_results = 32);

    // Ensures an isolated per-game save directory inside saves_root.
    std::optional<std::filesystem::path> ensure_save_directory(const std::filesystem::path& saves_root,
                                                               const std::string& game_id) const;

    // Loads clearly-labeled sample catalog entries (never marked Xbox-compatible).
    void seed_demo_fixtures();

    bool load_from_file(const std::filesystem::path& path, std::string* error_out = nullptr);
    bool save_to_file(const std::filesystem::path& path, std::string* error_out = nullptr) const;

private:
    std::filesystem::path sandbox_root_;
    std::vector<GameEntry> games_;
};

} // namespace xwine::runtime
