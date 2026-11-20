#include "../../tests/unit/test_framework.hpp"

#include "xwine/launcher/launcher_state.hpp"

#include <filesystem>

XWINE_TEST(LauncherStateNavigatesTabsFiltersDetailSettingsAndDiagnostics) {
    const auto tmp = std::filesystem::temp_directory_path() / "xwine_launcher_state_test";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);

    xwine::launcher::LauncherAppContext ctx;
    ctx.platform = xwine::platform::create_host_platform();
    ctx.data_dir = tmp;
    ctx.config.reset_to_defaults(tmp);
    ctx.library.seed_demo_fixtures();

    xwine::launcher::LauncherState st(ctx);
    XWINE_EXPECT_EQ(static_cast<int>(st.active_tab()), static_cast<int>(xwine::launcher::NavTab::Home));

    // Cycle tab with RB
    st.handle_action(xwine::input::UiAction::TabNext);
    XWINE_EXPECT_EQ(static_cast<int>(st.active_tab()), static_cast<int>(xwine::launcher::NavTab::Library));

    // Search filtering
    st.handle_action(xwine::input::UiAction::Menu);
    XWINE_EXPECT_TRUE(st.search_focused());
    st.handle_text_input("Vulkan");
    XWINE_EXPECT_EQ(st.visible_library_games().size(), 1U);
    st.handle_action(xwine::input::UiAction::Confirm); // Exit search focus
    XWINE_EXPECT_FALSE(st.search_focused());

    // Open game detail & execute honest dry-run
    st.handle_action(xwine::input::UiAction::Confirm);
    XWINE_EXPECT_TRUE(st.detail_open());
    st.execute_detail_action(0);
    XWINE_EXPECT_TRUE(st.last_launch_report().has_value());
    XWINE_EXPECT_FALSE(st.last_launch_report()->executed);
    st.handle_action(xwine::input::UiAction::Cancel);
    XWINE_EXPECT_FALSE(st.detail_open());

    // Sample Steam ACF parsing
    st.set_active_tab(xwine::launcher::NavTab::SteamLibrary);
    st.handle_action(xwine::input::UiAction::SecondaryX);
    XWINE_EXPECT_EQ(st.last_steam_scan().manifests.size(), 3U);

    // Settings mutation persists config.json
    st.set_active_tab(xwine::launcher::NavTab::Settings);
    st.adjust_setting(0, +1); // Switch to AppMode_1GB
    XWINE_EXPECT_EQ(ctx.config.config().runtime.xbox_target_mode, std::string("AppMode_1GB"));
    XWINE_EXPECT_TRUE(std::filesystem::exists(tmp / "config.json"));

    // Diagnostics bundle export
    st.set_active_tab(xwine::launcher::NavTab::Diagnostics);
    auto bundle = st.export_diagnostic_bundle();
    XWINE_EXPECT_TRUE(std::filesystem::exists(bundle));
}

int main() {
    return xwine::test::run_all_tests("XWine Launcher UI State Tests");
}
