#include "../unit/test_framework.hpp"
#include "../unit/test_pe_fixtures.hpp"

#include "xwine/platform/platform.hpp"
#include "xwine/runtime/library_manager.hpp"
#include "xwine/runtime/runtime_manager.hpp"
#include "xwine/steam/steam_library.hpp"

#include <filesystem>
#include <fstream>

XWINE_TEST(EndToEndSteamImportPeInspectAndHonestLaunchEvaluation) {
    const auto root = std::filesystem::temp_directory_path() / "xwine_integration_steam";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto steamapps = root / "steamapps";
    const auto game_dir = steamapps / "common" / "SampleDx11Game";
    std::filesystem::create_directories(game_dir, ec);

    // 1. Write genuine Steam AppManifest ACF
    {
        std::ofstream acf(steamapps / "appmanifest_999001.acf");
        acf << "\"AppState\"\n{\n"
            << "\t\"appid\"\t\t\"999001\"\n"
            << "\t\"name\"\t\t\"Sample DX11 Integration Title\"\n"
            << "\t\"StateFlags\"\t\"4\"\n"
            << "\t\"installdir\"\t\"SampleDx11Game\"\n"
            << "\t\"SizeOnDisk\"\t\"1073741824\"\n"
            << "\t\"buildid\"\t\"4242\"\n"
            << "}\n";
    }

    // 2. Write synthetic 64-bit PE32+ executable importing D3D11 + KERNEL32 + USER32
    {
        auto pe_bytes = xwine::test::build_synthetic_pe(
            true,
            {
                {"KERNEL32.dll", {"GetModuleHandleW", "ExitProcess"}},
                {"USER32.dll", {"CreateWindowExW"}},
                {"d3d11.dll", {"D3D11CreateDeviceAndSwapChain"}}
            });
        std::ofstream exe(game_dir / "SampleGame.exe", std::ios::binary);
        exe.write(reinterpret_cast<const char*>(pe_bytes.data()), static_cast<std::streamsize>(pe_bytes.size()));
    }

    // 3. Scan Steam library and import into LibraryManager
    auto scan = xwine::steam::SteamLibraryImporter::scan_steamapps_directory(steamapps);
    XWINE_EXPECT_TRUE(scan.root_found);
    XWINE_EXPECT_EQ(scan.manifests.size(), 1U);

    auto entry = xwine::steam::SteamLibraryImporter::to_game_entry(scan.manifests[0], true);
    XWINE_EXPECT_TRUE(entry.pe_inspected);
    XWINE_EXPECT_EQ(entry.pe_graphics_api, std::string("Direct3D 11"));
    XWINE_EXPECT_EQ(entry.pe_architecture, std::string("x86-64 (64-bit)"));
    XWINE_EXPECT_EQ(static_cast<int>(entry.compatibility),
                    static_cast<int>(xwine::runtime::CompatibilityRating::InspectOnly));

    xwine::runtime::LibraryManager lib;
    XWINE_EXPECT_TRUE(lib.add_or_update_game(entry));
    XWINE_EXPECT_TRUE(lib.save_to_file(root / "library.json"));

    // 4. Evaluate launch honestly via RuntimeManager
    auto plat = xwine::platform::create_host_platform();
    xwine::runtime::RuntimeManager rt;
    auto launch = rt.evaluate_launch_request(entry, plat->platform_id());
    XWINE_EXPECT_FALSE(launch.executed);
    XWINE_EXPECT_TRUE(launch.pe_report.valid_pe);
    XWINE_EXPECT_FALSE(launch.blockers.empty());

    // 5. Verify isolated per-game save folder creation
    auto save_dir = lib.ensure_save_directory(root / "saves", entry.id);
    XWINE_EXPECT_TRUE(save_dir.has_value());
    XWINE_EXPECT_TRUE(std::filesystem::exists(*save_dir));
}

int main() {
    return xwine::test::run_all_tests("XWine End-to-End Integration Tests");
}
