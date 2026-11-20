#pragma once

#include "xwine/platform/logger.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xwine::platform {

struct DisplayConfig {
    uint32_t window_width = 1280;
    uint32_t window_height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool uhd_4k_aware = true;
    bool smooth_animations = true;
    float ui_scale = 1.0f;
};

struct InputConfig {
    float left_stick_deadzone = 0.15f;
    float right_stick_deadzone = 0.15f;
    float trigger_threshold = 0.10f;
    float stick_sensitivity = 1.0f;
    std::string controller_profile = "XboxStandard";
    bool invert_y_axis = false;
};

struct RuntimeConfig {
    std::string preferred_graphics_strategy = "Auto";
    std::string xbox_target_mode = "GameMode_5GB"; // GameMode_5GB or AppMode_1GB
    uint64_t simulated_memory_cap_mib = 5120;
    bool enable_pe_deep_import_scan = true;
    bool show_experimental_warnings = true;
};

struct StorageConfig {
    std::string library_root_path;
    std::string steam_library_path;
    std::string saves_root_path;
};

struct AppConfig {
    uint32_t schema_version = 1;
    DisplayConfig display;
    InputConfig input;
    RuntimeConfig runtime;
    StorageConfig storage;
    LogLevel log_level = LogLevel::Info;
};

struct ConfigValidationResult {
    bool valid = true;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class ConfigManager {
public:
    ConfigManager() = default;

    AppConfig& config() { return config_; }
    const AppConfig& config() const { return config_; }

    void reset_to_defaults(const std::filesystem::path& data_dir);

    ConfigValidationResult validate_and_clamp();

    bool load_from_file(const std::filesystem::path& path, std::string* error_out = nullptr);
    bool save_to_file(const std::filesystem::path& path, std::string* error_out = nullptr) const;

private:
    AppConfig config_;
};

} // namespace xwine::platform
