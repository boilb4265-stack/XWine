#include "xwine/launcher/launcher_state.hpp"
#include "xwine/platform/json.hpp"
#include "xwine/platform/logger.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace xwine::launcher {

LauncherState::LauncherState(LauncherAppContext& ctx) : ctx_(ctx) {}

void LauncherState::set_active_tab(NavTab tab) {
    int idx = std::clamp(static_cast<int>(tab), 0, static_cast<int>(NavTab::Count) - 1);
    active_tab_ = static_cast<NavTab>(idx);
    search_focused_ = false;
    if (detail_open_) {
        detail_open_ = false;
    }
}

void LauncherState::set_library_filter(runtime::LibraryFilter f) {
    lib_filter_ = f;
    lib_selected_idx_ = 0;
    lib_scroll_row_ = 0;
}

void LauncherState::cycle_library_filter() {
    int next = (static_cast<int>(lib_filter_) + 1) % 6;
    set_library_filter(static_cast<runtime::LibraryFilter>(next));
}

void LauncherState::cycle_library_sort() {
    int next = (static_cast<int>(lib_sort_) + 1) % 4;
    lib_sort_ = static_cast<runtime::LibrarySort>(next);
    lib_selected_idx_ = 0;
    lib_scroll_row_ = 0;
}

void LauncherState::set_search_focused(bool focused) {
    search_focused_ = focused;
}

std::vector<const runtime::GameEntry*> LauncherState::home_featured_games() const {
    auto games = ctx_.library.query(runtime::LibraryFilter::All, runtime::LibrarySort::RecentlyPlayedDesc);
    if (games.size() > 5) games.resize(5);
    return games;
}

std::vector<const runtime::GameEntry*> LauncherState::visible_library_games() const {
    return ctx_.library.query(lib_filter_, lib_sort_, search_query_);
}

void LauncherState::select_library_index(int idx) {
    auto vis = visible_library_games();
    if (vis.empty()) {
        lib_selected_idx_ = 0;
        lib_scroll_row_ = 0;
        return;
    }
    lib_selected_idx_ = std::clamp(idx, 0, static_cast<int>(vis.size()) - 1);
    const int row = lib_selected_idx_ / kLibraryColumns;
    if (row < lib_scroll_row_) lib_scroll_row_ = row;
    if (row >= lib_scroll_row_ + 2) lib_scroll_row_ = row - 1;
}

const runtime::GameEntry* LauncherState::detail_game() const {
    if (!detail_open_ || detail_game_id_.empty()) return nullptr;
    return ctx_.library.find_game(detail_game_id_);
}

void LauncherState::open_game_detail(const std::string& game_id) {
    if (!ctx_.library.find_game(game_id)) return;
    detail_game_id_ = game_id;
    detail_open_ = true;
    detail_enter_anim_ = ctx_.config.config().display.smooth_animations ? 0.0f : 1.0f;
    detail_action_idx_ = 0;
    search_focused_ = false;
    last_launch_report_.reset();
}

void LauncherState::close_game_detail() {
    detail_open_ = false;
    detail_enter_anim_ = 0.0f;
    last_launch_report_.reset();
}

float LauncherState::press_feedback_scale() const {
    // Emil Design-Eng: subtle 0.97 scale dip on press, snappy 140ms ease-out recovery
    return 1.0f - 0.03f * std::clamp(press_anim_, 0.0f, 1.0f);
}

void LauncherState::execute_detail_action(int action_idx) {
    auto* game = ctx_.library.find_game(detail_game_id_);
    if (!game) return;
    press_anim_ = 1.0f;

    if (action_idx == 0) {
        // Honest PE inspection & launch feasibility check — NEVER pretends to run the game.
        ctx_.library.record_inspection_event(game->id);
        last_launch_report_ = ctx_.runtime.evaluate_launch_request(*game, ctx_.platform->platform_id());
        if (!game->install_directory.empty() && !game->executable_relative_path.empty()) {
            ctx_.library.inspect_game_executable(game->id);
        }
        save_all_state();
        show_toast("Execution blocked — PE & Xbox blockers logged", true, 4.0f);
    } else if (action_idx == 1) {
        ctx_.library.toggle_favorite(game->id);
        save_all_state();
        show_toast(game->favorite ? "Added to favorites" : "Removed from favorites", false, 2.2f);
    } else if (action_idx == 2) {
        static const char* kBackends[] = {"Auto", "D3D12 Pass-Through", "D3D11 Shim", "D3D9On12"};
        int cur = 0;
        for (int i = 0; i < 4; ++i) {
            if (game->launch_options.graphics_backend == kBackends[i]) {
                cur = i;
                break;
            }
        }
        game->launch_options.graphics_backend = kBackends[(cur + 1) % 4];
        save_all_state();
        show_toast("Graphics strategy: " + game->launch_options.graphics_backend, false, 2.2f);
    } else if (action_idx == 3) {
        close_game_detail();
    }
}

void LauncherState::scan_configured_steam_library() {
    const std::string& steam_path = ctx_.config.config().storage.steam_library_path;
    platform::Logger::instance().info("Steam", "Scanning Steam library path: " + steam_path);
    last_steam_scan_ = steam::SteamLibraryImporter::scan_steamapps_directory(steam_path);

    if (!last_steam_scan_.root_found) {
        show_toast("Steam directory not found at configured path (try Sample Fixtures).", true, 4.5f);
        return;
    }

    int imported = 0;
    for (const auto& m : last_steam_scan_.manifests) {
        auto entry = steam::SteamLibraryImporter::to_game_entry(m, true);
        if (ctx_.library.add_or_update_game(entry)) {
            ++imported;
        }
    }
    save_all_state();
    show_toast("Imported " + std::to_string(imported) + " Steam manifest(s) (metadata only).", false, 4.0f);
}

void LauncherState::load_sample_steam_manifests() {
    // Parse genuine Valve ACF syntax in-memory via VdfParser/SteamCompatibilityEvaluator
    // so users on machines without Steam installed can test real metadata ingestion.
    const std::filesystem::path fixture_dir = ctx_.data_dir / "steam_sample_fixtures";
    std::error_code ec;
    std::filesystem::create_directories(fixture_dir, ec);

    auto write_acf = [&](const std::string& filename, const std::string& content) {
        std::ofstream out(fixture_dir / filename, std::ios::out | std::ios::trunc);
        out << content;
    };

    write_acf("appmanifest_620.acf",
              "\"AppState\"\n{\n"
              "\t\"appid\"\t\t\"620\"\n"
              "\t\"name\"\t\t\"Portal 2 (Sample ACF Metadata)\"\n"
              "\t\"StateFlags\"\t\t\"4\"\n"
              "\t\"installdir\"\t\t\"Portal 2\"\n"
              "\t\"SizeOnDisk\"\t\t\"12884901888\"\n"
              "\t\"buildid\"\t\t\"10421810\"\n"
              "}\n");
    write_acf("appmanifest_413150.acf",
              "\"AppState\"\n{\n"
              "\t\"appid\"\t\t\"413150\"\n"
              "\t\"name\"\t\t\"Stardew Valley (Sample ACF Metadata)\"\n"
              "\t\"StateFlags\"\t\t\"4\"\n"
              "\t\"installdir\"\t\t\"Stardew Valley\"\n"
              "\t\"SizeOnDisk\"\t\t\"681574400\"\n"
              "\t\"buildid\"\t\t\"15892211\"\n"
              "}\n");
    write_acf("appmanifest_367520.acf",
              "\"AppState\"\n{\n"
              "\t\"appid\"\t\t\"367520\"\n"
              "\t\"name\"\t\t\"Hollow Knight (Sample ACF Metadata)\"\n"
              "\t\"StateFlags\"\t\t\"4\"\n"
              "\t\"installdir\"\t\t\"Hollow Knight\"\n"
              "\t\"SizeOnDisk\"\t\t\"7950000000\"\n"
              "\t\"buildid\"\t\t\"7123456\"\n"
              "}\n");

    last_steam_scan_ = steam::SteamLibraryImporter::scan_steamapps_directory(fixture_dir);
    int imported = 0;
    for (const auto& m : last_steam_scan_.manifests) {
        auto entry = steam::SteamLibraryImporter::to_game_entry(m, false);
        if (ctx_.library.add_or_update_game(entry)) ++imported;
    }
    save_all_state();
    platform::Logger::instance().info("Steam", "Parsed " + std::to_string(imported) + " sample ACF manifests.");
    show_toast("Parsed " + std::to_string(imported) + " sample Steam ACF manifests (metadata only).", false, 4.0f);
}

void LauncherState::adjust_setting(int index, int direction) {
    auto& cfg = ctx_.config.config();
    auto step = [direction](float& val, float delta, float lo, float hi) {
        int d = (direction == 0) ? 1 : direction;
        val = std::clamp(val + static_cast<float>(d) * delta, lo, hi);
    };

    switch (index) {
        case 0: // Xbox target mode
            cfg.runtime.xbox_target_mode =
                (cfg.runtime.xbox_target_mode == "GameMode_5GB") ? "AppMode_1GB" : "GameMode_5GB";
            break;
        case 1: step(cfg.input.left_stick_deadzone, 0.05f, 0.05f, 0.45f); break;
        case 2: step(cfg.input.right_stick_deadzone, 0.05f, 0.05f, 0.45f); break;
        case 3: step(cfg.input.stick_sensitivity, 0.10f, 0.50f, 2.00f); break;
        case 4: step(cfg.input.trigger_threshold, 0.05f, 0.05f, 0.50f); break;
        case 5: {
            static const char* kProfiles[] = {"XboxStandard", "SwappedConfirmCancel", "SwapXY"};
            int cur = 0;
            for (int i = 0; i < 3; ++i) {
                if (cfg.input.controller_profile == kProfiles[i]) cur = i;
            }
            int d = (direction == 0) ? 1 : direction;
            cur = (cur + d + 3) % 3;
            cfg.input.controller_profile = kProfiles[cur];
            break;
        }
        case 6: cfg.input.invert_y_axis = !cfg.input.invert_y_axis; break;
        case 7: cfg.display.smooth_animations = !cfg.display.smooth_animations; break;
        case 8: cfg.display.uhd_4k_aware = !cfg.display.uhd_4k_aware; break;
        case 9: {
            int lv = static_cast<int>(cfg.log_level);
            int d = (direction == 0) ? 1 : direction;
            lv = (lv + d + 5) % 5;
            cfg.log_level = static_cast<platform::LogLevel>(lv);
            platform::Logger::instance().set_min_level(cfg.log_level);
            break;
        }
        case 10:
            ctx_.config.validate_and_clamp();
            save_all_state();
            show_toast("Configuration saved to config.json.", false, 3.0f);
            config_dirty_ = true;
            return;
        case 11:
            ctx_.config.reset_to_defaults(ctx_.data_dir);
            platform::Logger::instance().set_min_level(cfg.log_level);
            save_all_state();
            show_toast("Settings reset to defaults.", false, 3.0f);
            config_dirty_ = true;
            return;
        default:
            return;
    }

    ctx_.config.validate_and_clamp();
    save_all_state();
    config_dirty_ = true;
}

void LauncherState::cycle_diag_log_filter() {
    switch (diag_log_filter_) {
        case platform::LogLevel::Trace: diag_log_filter_ = platform::LogLevel::Info; break;
        case platform::LogLevel::Info:  diag_log_filter_ = platform::LogLevel::Warn; break;
        case platform::LogLevel::Warn:  diag_log_filter_ = platform::LogLevel::Error; break;
        default:                        diag_log_filter_ = platform::LogLevel::Trace; break;
    }
}

std::filesystem::path LauncherState::export_diagnostic_bundle() {
    const auto diag_dir = ctx_.data_dir / "diagnostics";
    std::error_code ec;
    std::filesystem::create_directories(diag_dir, ec);

    const auto log_path = diag_dir / "xwine_recent.log";
    platform::Logger::instance().export_to_file(log_path);

    const auto report_path = diag_dir / "xwine_diagnostics.json";
    const auto host = ctx_.platform->host_hardware();
    const bool game_mode = ctx_.config.config().runtime.xbox_target_mode == "GameMode_5GB";
    const auto xbox = ctx_.platform->target_xbox_series_s_profile(game_mode);
    const auto metrics = ctx_.platform->query_metrics(xbox.app_usable_ram_bytes);
    const auto fstats = ctx_.benchmark.frame_stats();

    platform::JsonValue root = platform::JsonValue::make_object();
    root["project"] = "Project XWine (Experimental)";
    root["host_platform"] = platform::to_string(ctx_.platform->platform_id());
    root["host_cpu"] = host.cpu_model;
    root["host_ram_bytes"] = host.physical_ram_bytes;
    root["process_rss_bytes"] = metrics.process_rss_bytes;
    root["xbox_target_device"] = xbox.device_name;
    root["xbox_hardware_verified"] = xbox.hardware_verified;
    root["xbox_usable_ram_cap_bytes"] = xbox.app_usable_ram_bytes;
    root["avg_fps"] = fstats.current_fps;
    root["avg_frame_ms"] = fstats.avg_frame_ms;
    root["p99_frame_ms"] = fstats.p99_frame_ms;
    root["library_count"] = static_cast<uint64_t>(ctx_.library.all_games().size());
    root["log_warn_count"] = static_cast<uint64_t>(platform::Logger::instance().warn_count());
    root["log_error_count"] = static_cast<uint64_t>(platform::Logger::instance().error_count());

    std::ofstream out(report_path, std::ios::out | std::ios::trunc);
    out << root.dump(2) << "\n";
    platform::Logger::instance().info("Diagnostics", "Exported diagnostic bundle to " + report_path.string());
    show_toast("Exported diagnostics to " + report_path.filename().string(), false, 4.0f);
    return report_path;
}

void LauncherState::show_toast(const std::string& msg, bool is_warning, float seconds) {
    toast_.message = msg;
    toast_.is_warning = is_warning;
    toast_.remaining_seconds = seconds;
}

void LauncherState::save_all_state() {
    ctx_.config.save_to_file(ctx_.data_dir / "config.json");
    ctx_.library.save_to_file(ctx_.data_dir / "library.json");
}

void LauncherState::handle_text_input(const std::string& text) {
    if (!search_focused_) return;
    if (search_query_.size() + text.size() <= 48) {
        search_query_ += text;
        lib_selected_idx_ = 0;
        lib_scroll_row_ = 0;
    }
}

void LauncherState::handle_backspace() {
    if (!search_focused_ || search_query_.empty()) return;
    search_query_.pop_back();
    lib_selected_idx_ = 0;
    lib_scroll_row_ = 0;
}

void LauncherState::handle_action(input::UiAction action) {
    if (action == input::UiAction::None) return;

    if (search_focused_) {
        if (action == input::UiAction::Cancel || action == input::UiAction::Confirm) {
            search_focused_ = false;
        }
        return;
    }

    if (detail_open_) {
        switch (action) {
            case input::UiAction::Cancel:
                close_game_detail();
                break;
            case input::UiAction::NavUp:
            case input::UiAction::NavLeft:
                detail_action_idx_ = (detail_action_idx_ + 3) % 4;
                break;
            case input::UiAction::NavDown:
            case input::UiAction::NavRight:
                detail_action_idx_ = (detail_action_idx_ + 1) % 4;
                break;
            case input::UiAction::Confirm:
                execute_detail_action(detail_action_idx_);
                break;
            case input::UiAction::SecondaryX:
                execute_detail_action(0);
                break;
            case input::UiAction::TertiaryY:
                execute_detail_action(1);
                break;
            default:
                break;
        }
        return;
    }

    // Global tab switching via LB / RB
    if (action == input::UiAction::TabPrev) {
        int prev = (static_cast<int>(active_tab_) - 1 + static_cast<int>(NavTab::Count)) %
                   static_cast<int>(NavTab::Count);
        set_active_tab(static_cast<NavTab>(prev));
        return;
    }
    if (action == input::UiAction::TabNext) {
        int next = (static_cast<int>(active_tab_) + 1) % static_cast<int>(NavTab::Count);
        set_active_tab(static_cast<NavTab>(next));
        return;
    }
    if (action == input::UiAction::View) {
        set_active_tab(NavTab::Diagnostics);
        return;
    }

    switch (active_tab_) {
        case NavTab::Home: {
            auto featured = home_featured_games();
            const int n = static_cast<int>(featured.size());
            if (n == 0) break;
            if (action == input::UiAction::NavLeft) home_selected_idx_ = std::max(0, home_selected_idx_ - 1);
            else if (action == input::UiAction::NavRight) home_selected_idx_ = std::min(n - 1, home_selected_idx_ + 1);
            else if (action == input::UiAction::Confirm) open_game_detail(featured[home_selected_idx_]->id);
            else if (action == input::UiAction::TertiaryY) {
                ctx_.library.toggle_favorite(featured[home_selected_idx_]->id);
                save_all_state();
            } else if (action == input::UiAction::Menu) {
                set_active_tab(NavTab::Library);
                set_search_focused(true);
            }
            break;
        }

        case NavTab::Library: {
            auto vis = visible_library_games();
            const int n = static_cast<int>(vis.size());
            if (action == input::UiAction::Menu) {
                search_focused_ = true;
            } else if (action == input::UiAction::SecondaryX) {
                cycle_library_filter();
            } else if (action == input::UiAction::TertiaryY) {
                cycle_library_sort();
            } else if (n > 0) {
                if (action == input::UiAction::NavLeft) select_library_index(lib_selected_idx_ - 1);
                else if (action == input::UiAction::NavRight) select_library_index(lib_selected_idx_ + 1);
                else if (action == input::UiAction::NavUp) select_library_index(lib_selected_idx_ - kLibraryColumns);
                else if (action == input::UiAction::NavDown) select_library_index(lib_selected_idx_ + kLibraryColumns);
                else if (action == input::UiAction::Confirm) {
                    open_game_detail(vis[static_cast<size_t>(lib_selected_idx_)]->id);
                }
            }
            break;
        }

        case NavTab::SteamLibrary: {
            if (action == input::UiAction::Confirm) {
                scan_configured_steam_library();
            } else if (action == input::UiAction::SecondaryX) {
                load_sample_steam_manifests();
            } else if (action == input::UiAction::NavUp) {
                steam_selected_idx_ = std::max(0, steam_selected_idx_ - 1);
            } else if (action == input::UiAction::NavDown) {
                int max_i = std::max(0, static_cast<int>(last_steam_scan_.manifests.size()) - 1);
                steam_selected_idx_ = std::min(max_i, steam_selected_idx_ + 1);
            }
            break;
        }

        case NavTab::Compatibility: {
            if (action == input::UiAction::NavUp) compat_scroll_ = std::max(0, compat_scroll_ - 1);
            else if (action == input::UiAction::NavDown) compat_scroll_ = std::min(14, compat_scroll_ + 1);
            break;
        }

        case NavTab::Runtimes: {
            if (action == input::UiAction::NavUp) runtime_scroll_ = std::max(0, runtime_scroll_ - 1);
            else if (action == input::UiAction::NavDown) runtime_scroll_ = std::min(8, runtime_scroll_ + 1);
            break;
        }

        case NavTab::Settings: {
            if (action == input::UiAction::NavUp) {
                settings_selected_idx_ = (settings_selected_idx_ - 1 + kSettingsItemCount) % kSettingsItemCount;
            } else if (action == input::UiAction::NavDown) {
                settings_selected_idx_ = (settings_selected_idx_ + 1) % kSettingsItemCount;
            } else if (action == input::UiAction::NavLeft) {
                adjust_setting(settings_selected_idx_, -1);
            } else if (action == input::UiAction::NavRight) {
                adjust_setting(settings_selected_idx_, +1);
            } else if (action == input::UiAction::Confirm) {
                adjust_setting(settings_selected_idx_, 0);
            }
            break;
        }

        case NavTab::Diagnostics: {
            if (action == input::UiAction::Confirm) {
                export_diagnostic_bundle();
            } else if (action == input::UiAction::SecondaryX) {
                cycle_diag_log_filter();
            }
            break;
        }

        case NavTab::Count:
            break;
    }
}

void LauncherState::update_animations(float dt_seconds) {
    const float clamped_dt = std::clamp(dt_seconds, 0.0f, 0.1f);
    const float target_tab = static_cast<float>(active_tab_);
    if (ctx_.config.config().display.smooth_animations) {
        // Fast ease-out settle (~160ms): responsive, interruptible, zero looping idle motion
        const float tab_rate = 1.0f - std::exp(-18.0f * clamped_dt);
        anim_tab_index_ += (target_tab - anim_tab_index_) * tab_rate;
        if (std::fabs(target_tab - anim_tab_index_) < 0.002f) anim_tab_index_ = target_tab;

        if (detail_open_) {
            const float modal_rate = 1.0f - std::exp(-20.0f * clamped_dt);
            detail_enter_anim_ += (1.0f - detail_enter_anim_) * modal_rate;
            if (detail_enter_anim_ > 0.995f) detail_enter_anim_ = 1.0f;
        }
        if (press_anim_ > 0.0f) {
            press_anim_ = std::max(0.0f, press_anim_ - clamped_dt / 0.14f);
        }
    } else {
        anim_tab_index_ = target_tab;
        detail_enter_anim_ = detail_open_ ? 1.0f : 0.0f;
        press_anim_ = 0.0f;
    }

    if (toast_.remaining_seconds > 0.0f) {
        toast_.remaining_seconds = std::max(0.0f, toast_.remaining_seconds - clamped_dt);
    }
}

} // namespace xwine::launcher
