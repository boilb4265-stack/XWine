#pragma once

#include "xwine/graphics/graphics_backend.hpp"
#include "xwine/input/controller_mapping.hpp"
#include "xwine/platform/benchmark.hpp"
#include "xwine/platform/config.hpp"
#include "xwine/platform/platform.hpp"
#include "xwine/runtime/library_manager.hpp"
#include "xwine/runtime/runtime_manager.hpp"
#include "xwine/steam/steam_library.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xwine::launcher {

enum class NavTab : int {
    Home = 0,
    Library = 1,
    SteamLibrary = 2,
    Compatibility = 3,
    Runtimes = 4,
    Settings = 5,
    Diagnostics = 6,
    Count = 7
};

inline const char* nav_tab_label(NavTab tab) {
    switch (tab) {
        case NavTab::Home:          return "HOME";
        case NavTab::Library:       return "LIBRARY";
        case NavTab::SteamLibrary:  return "STEAM LIBRARY";
        case NavTab::Compatibility: return "COMPATIBILITY";
        case NavTab::Runtimes:      return "RUNTIMES";
        case NavTab::Settings:      return "SETTINGS";
        case NavTab::Diagnostics:   return "DIAGNOSTICS";
        case NavTab::Count:         return "";
    }
    return "";
}

struct ToastNotification {
    std::string message;
    bool is_warning = false;
    float remaining_seconds = 0.0f;
};

struct LauncherAppContext {
    std::filesystem::path data_dir;
    std::unique_ptr<platform::IPlatform> platform;
    platform::ConfigManager config;
    platform::BenchmarkTracker benchmark;
    runtime::LibraryManager library;
    runtime::RuntimeManager runtime;
    graphics::GraphicsCapabilityRegistry graphics;
};

class LauncherState {
public:
    explicit LauncherState(LauncherAppContext& ctx);

    void handle_action(input::UiAction action);
    void handle_text_input(const std::string& text);
    void handle_backspace();
    void update_animations(float dt_seconds);

    // Tab navigation
    NavTab active_tab() const { return active_tab_; }
    void set_active_tab(NavTab tab);
    float animated_tab_index() const { return anim_tab_index_; }

    // Home view state
    int home_selected_index() const { return home_selected_idx_; }
    std::vector<const runtime::GameEntry*> home_featured_games() const;

    // Library view state
    runtime::LibraryFilter library_filter() const { return lib_filter_; }
    runtime::LibrarySort library_sort() const { return lib_sort_; }
    void set_library_filter(runtime::LibraryFilter f);
    void cycle_library_filter();
    void cycle_library_sort();
    const std::string& search_query() const { return search_query_; }
    bool search_focused() const { return search_focused_; }
    void set_search_focused(bool focused);
    std::vector<const runtime::GameEntry*> visible_library_games() const;
    int library_selected_index() const { return lib_selected_idx_; }
    void select_library_index(int idx);
    int library_scroll_row() const { return lib_scroll_row_; }
    float detail_enter_progress() const { return detail_enter_anim_; }
    float press_feedback_scale() const;

    // Game Detail view
    bool detail_open() const { return detail_open_; }
    const runtime::GameEntry* detail_game() const;
    int detail_action_index() const { return detail_action_idx_; }
    void open_game_detail(const std::string& game_id);
    void close_game_detail();
    void execute_detail_action(int action_idx);
    const std::optional<runtime::LaunchAttemptReport>& last_launch_report() const {
        return last_launch_report_;
    }

    // Steam view
    const steam::SteamScanReport& last_steam_scan() const { return last_steam_scan_; }
    int steam_selected_index() const { return steam_selected_idx_; }
    void scan_configured_steam_library();
    void load_sample_steam_manifests();
    void scan_local_apps_and_usb();

    // Compatibility & Runtimes scroll
    int compat_scroll_offset() const { return compat_scroll_; }
    int runtime_scroll_offset() const { return runtime_scroll_; }

    // Settings view
    int settings_selected_index() const { return settings_selected_idx_; }
    static constexpr int kSettingsItemCount = 12;
    void adjust_setting(int index, int direction); // direction: -1, 0 (toggle/activate), +1

    // Diagnostics view
    platform::LogLevel diag_log_filter() const { return diag_log_filter_; }
    void cycle_diag_log_filter();
    std::filesystem::path export_diagnostic_bundle();

    // Notifications & status
    const ToastNotification& toast() const { return toast_; }
    void show_toast(const std::string& msg, bool is_warning = false, float seconds = 4.0f);

    bool config_dirty() const { return config_dirty_; }
    void clear_config_dirty() { config_dirty_ = false; }

    LauncherAppContext& context() { return ctx_; }
    const LauncherAppContext& context() const { return ctx_; }

private:
    void save_all_state();

    LauncherAppContext& ctx_;
    NavTab active_tab_ = NavTab::Home;
    float anim_tab_index_ = 0.0f;
    float detail_enter_anim_ = 0.0f;
    float press_anim_ = 0.0f;

    int home_selected_idx_ = 0;

    runtime::LibraryFilter lib_filter_ = runtime::LibraryFilter::All;
    runtime::LibrarySort lib_sort_ = runtime::LibrarySort::TitleAsc;
    std::string search_query_;
    bool search_focused_ = false;
    int lib_selected_idx_ = 0;
    int lib_scroll_row_ = 0;
    static constexpr int kLibraryColumns = 5;

    bool detail_open_ = false;
    std::string detail_game_id_;
    int detail_action_idx_ = 0; // 0: Inspect/Dry-Run, 1: Favorite, 2: Cycle Profile, 3: Back
    std::optional<runtime::LaunchAttemptReport> last_launch_report_;

    steam::SteamScanReport last_steam_scan_;
    int steam_selected_idx_ = 0;

    int compat_scroll_ = 0;
    int runtime_scroll_ = 0;

    int settings_selected_idx_ = 0;
    bool config_dirty_ = false;

    platform::LogLevel diag_log_filter_ = platform::LogLevel::Trace;
    ToastNotification toast_;
};

} // namespace xwine::launcher
