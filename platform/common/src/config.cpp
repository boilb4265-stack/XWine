#include "xwine/platform/config.hpp"
#include "xwine/platform/json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace xwine::platform {

void ConfigManager::reset_to_defaults(const std::filesystem::path& data_dir) {
    config_ = AppConfig{};
    config_.storage.library_root_path = (data_dir / "games").string();
    config_.storage.saves_root_path = (data_dir / "saves").string();
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        config_.storage.steam_library_path =
            (std::filesystem::path(home) / "Library/Application Support/Steam/steamapps").string();
    }
#endif
}

ConfigValidationResult ConfigManager::validate_and_clamp() {
    ConfigValidationResult res;

    if (config_.display.window_width < 640 || config_.display.window_width > 7680) {
        res.warnings.push_back("window_width out of range [640..7680]; clamped.");
        config_.display.window_width = std::clamp<uint32_t>(config_.display.window_width, 640, 7680);
    }
    if (config_.display.window_height < 360 || config_.display.window_height > 4320) {
        res.warnings.push_back("window_height out of range [360..4320]; clamped.");
        config_.display.window_height = std::clamp<uint32_t>(config_.display.window_height, 360, 4320);
    }
    if (config_.display.ui_scale < 0.75f || config_.display.ui_scale > 2.0f) {
        res.warnings.push_back("ui_scale out of range [0.75..2.0]; clamped.");
        config_.display.ui_scale = std::clamp(config_.display.ui_scale, 0.75f, 2.0f);
    }

    if (config_.input.left_stick_deadzone < 0.0f || config_.input.left_stick_deadzone > 0.6f) {
        res.warnings.push_back("left_stick_deadzone out of range [0.0..0.6]; clamped.");
        config_.input.left_stick_deadzone = std::clamp(config_.input.left_stick_deadzone, 0.0f, 0.6f);
    }
    if (config_.input.right_stick_deadzone < 0.0f || config_.input.right_stick_deadzone > 0.6f) {
        res.warnings.push_back("right_stick_deadzone out of range [0.0..0.6]; clamped.");
        config_.input.right_stick_deadzone = std::clamp(config_.input.right_stick_deadzone, 0.0f, 0.6f);
    }
    if (config_.input.trigger_threshold < 0.01f || config_.input.trigger_threshold > 0.9f) {
        res.warnings.push_back("trigger_threshold out of range [0.01..0.9]; clamped.");
        config_.input.trigger_threshold = std::clamp(config_.input.trigger_threshold, 0.01f, 0.9f);
    }
    if (config_.input.stick_sensitivity < 0.25f || config_.input.stick_sensitivity > 3.0f) {
        res.warnings.push_back("stick_sensitivity out of range [0.25..3.0]; clamped.");
        config_.input.stick_sensitivity = std::clamp(config_.input.stick_sensitivity, 0.25f, 3.0f);
    }

    if (config_.runtime.xbox_target_mode != "GameMode_5GB" &&
        config_.runtime.xbox_target_mode != "AppMode_1GB") {
        res.warnings.push_back("Unknown xbox_target_mode; reset to GameMode_5GB.");
        config_.runtime.xbox_target_mode = "GameMode_5GB";
    }
    config_.runtime.simulated_memory_cap_mib =
        (config_.runtime.xbox_target_mode == "AppMode_1GB") ? 1024 : 5120;

    return res;
}

bool ConfigManager::load_from_file(const std::filesystem::path& path, std::string* error_out) {
    std::ifstream in(path);
    if (!in.is_open()) {
        if (error_out) *error_out = "Could not open config file: " + path.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = JsonValue::parse(ss.str());
    if (!parsed.ok || !parsed.value.is_object()) {
        if (error_out) *error_out = "Config JSON parse error: " + parsed.error;
        return false;
    }

    const JsonValue& root = parsed.value;
    if (auto* v = root.get("schema_version")) {
        config_.schema_version = static_cast<uint32_t>(v->as_uint(1));
    }
    if (auto* v = root.get("log_level")) {
        config_.log_level = parse_log_level(v->as_string_or("INFO"));
    }

    if (auto* d = root.get("display"); d && d->is_object()) {
        if (auto* x = d->get("window_width")) config_.display.window_width = static_cast<uint32_t>(x->as_uint(1280));
        if (auto* x = d->get("window_height")) config_.display.window_height = static_cast<uint32_t>(x->as_uint(720));
        if (auto* x = d->get("fullscreen")) config_.display.fullscreen = x->as_bool(false);
        if (auto* x = d->get("vsync")) config_.display.vsync = x->as_bool(true);
        if (auto* x = d->get("uhd_4k_aware")) config_.display.uhd_4k_aware = x->as_bool(true);
        if (auto* x = d->get("smooth_animations")) config_.display.smooth_animations = x->as_bool(true);
        if (auto* x = d->get("ui_scale")) config_.display.ui_scale = static_cast<float>(x->as_double(1.0));
    }

    if (auto* i = root.get("input"); i && i->is_object()) {
        if (auto* x = i->get("left_stick_deadzone")) config_.input.left_stick_deadzone = static_cast<float>(x->as_double(0.15));
        if (auto* x = i->get("right_stick_deadzone")) config_.input.right_stick_deadzone = static_cast<float>(x->as_double(0.15));
        if (auto* x = i->get("trigger_threshold")) config_.input.trigger_threshold = static_cast<float>(x->as_double(0.10));
        if (auto* x = i->get("stick_sensitivity")) config_.input.stick_sensitivity = static_cast<float>(x->as_double(1.0));
        if (auto* x = i->get("controller_profile")) config_.input.controller_profile = x->as_string_or("XboxStandard");
        if (auto* x = i->get("invert_y_axis")) config_.input.invert_y_axis = x->as_bool(false);
    }

    if (auto* r = root.get("runtime"); r && r->is_object()) {
        if (auto* x = r->get("preferred_graphics_strategy")) config_.runtime.preferred_graphics_strategy = x->as_string_or("Auto");
        if (auto* x = r->get("xbox_target_mode")) config_.runtime.xbox_target_mode = x->as_string_or("GameMode_5GB");
        if (auto* x = r->get("enable_pe_deep_import_scan")) config_.runtime.enable_pe_deep_import_scan = x->as_bool(true);
        if (auto* x = r->get("show_experimental_warnings")) config_.runtime.show_experimental_warnings = x->as_bool(true);
    }

    if (auto* s = root.get("storage"); s && s->is_object()) {
        if (auto* x = s->get("library_root_path")) config_.storage.library_root_path = x->as_string();
        if (auto* x = s->get("steam_library_path")) config_.storage.steam_library_path = x->as_string();
        if (auto* x = s->get("saves_root_path")) config_.storage.saves_root_path = x->as_string();
    }

    validate_and_clamp();
    return true;
}

bool ConfigManager::save_to_file(const std::filesystem::path& path, std::string* error_out) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error_out) *error_out = "Failed to create config parent directory: " + ec.message();
            return false;
        }
    }

    JsonValue root = JsonValue::make_object();
    root["schema_version"] = static_cast<uint64_t>(config_.schema_version);
    root["log_level"] = log_level_name(config_.log_level);

    JsonValue d = JsonValue::make_object();
    d["window_width"] = static_cast<uint64_t>(config_.display.window_width);
    d["window_height"] = static_cast<uint64_t>(config_.display.window_height);
    d["fullscreen"] = config_.display.fullscreen;
    d["vsync"] = config_.display.vsync;
    d["uhd_4k_aware"] = config_.display.uhd_4k_aware;
    d["smooth_animations"] = config_.display.smooth_animations;
    d["ui_scale"] = static_cast<double>(config_.display.ui_scale);
    root["display"] = std::move(d);

    JsonValue i = JsonValue::make_object();
    i["left_stick_deadzone"] = static_cast<double>(config_.input.left_stick_deadzone);
    i["right_stick_deadzone"] = static_cast<double>(config_.input.right_stick_deadzone);
    i["trigger_threshold"] = static_cast<double>(config_.input.trigger_threshold);
    i["stick_sensitivity"] = static_cast<double>(config_.input.stick_sensitivity);
    i["controller_profile"] = config_.input.controller_profile;
    i["invert_y_axis"] = config_.input.invert_y_axis;
    root["input"] = std::move(i);

    JsonValue r = JsonValue::make_object();
    r["preferred_graphics_strategy"] = config_.runtime.preferred_graphics_strategy;
    r["xbox_target_mode"] = config_.runtime.xbox_target_mode;
    r["simulated_memory_cap_mib"] = config_.runtime.simulated_memory_cap_mib;
    r["enable_pe_deep_import_scan"] = config_.runtime.enable_pe_deep_import_scan;
    r["show_experimental_warnings"] = config_.runtime.show_experimental_warnings;
    root["runtime"] = std::move(r);

    JsonValue s = JsonValue::make_object();
    s["library_root_path"] = config_.storage.library_root_path;
    s["steam_library_path"] = config_.storage.steam_library_path;
    s["saves_root_path"] = config_.storage.saves_root_path;
    root["storage"] = std::move(s);

    const std::filesystem::path tmp_path = path.string() + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            if (error_out) *error_out = "Could not open temporary config file for writing.";
            return false;
        }
        out << root.dump(2) << "\n";
        if (!out.good()) {
            if (error_out) *error_out = "Write error while saving config file.";
            return false;
        }
    }
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        if (error_out) *error_out = "Failed atomic rename for config file: " + ec.message();
        return false;
    }
    return true;
}

} // namespace xwine::platform
