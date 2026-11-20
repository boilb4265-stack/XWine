#include "test_framework.hpp"
#include "test_pe_fixtures.hpp"

#include "xwine/graphics/graphics_backend.hpp"
#include "xwine/input/controller_mapping.hpp"
#include "xwine/platform/benchmark.hpp"
#include "xwine/platform/config.hpp"
#include "xwine/platform/json.hpp"
#include "xwine/platform/logger.hpp"
#include "xwine/platform/platform.hpp"
#include "xwine/platform/xbox_capabilities.hpp"
#include "xwine/runtime/api_catalog.hpp"
#include "xwine/runtime/library_manager.hpp"
#include "xwine/runtime/pe_inspector.hpp"
#include "xwine/runtime/process_model.hpp"
#include "xwine/runtime/virtual_memory.hpp"
#include "xwine/steam/vdf_parser.hpp"

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path make_temp_test_dir(const std::string& suffix) {
    auto p = std::filesystem::temp_directory_path() / ("xwine_unit_" + suffix);
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

} // namespace

XWINE_TEST(JsonRoundTripAndMalformedRejection) {
    using xwine::platform::JsonValue;
    JsonValue obj = JsonValue::make_object();
    obj["name"] = "XWine \"Lab\"\nLine2";
    obj["count"] = 42;
    obj["ratio"] = 0.75;
    obj["enabled"] = true;
    JsonValue arr = JsonValue::make_array();
    arr.push_back("alpha");
    arr.push_back(123);
    obj["items"] = std::move(arr);

    const std::string dumped = obj.dump(2);
    auto parsed = JsonValue::parse(dumped);
    XWINE_EXPECT_TRUE(parsed.ok);
    XWINE_EXPECT_EQ(parsed.value["name"].as_string(), std::string("XWine \"Lab\"\nLine2"));
    XWINE_EXPECT_EQ(parsed.value["count"].as_int(), 42);
    XWINE_EXPECT_NEAR(parsed.value["ratio"].as_double(), 0.75, 1e-6);
    XWINE_EXPECT_TRUE(parsed.value["enabled"].as_bool());
    XWINE_EXPECT_EQ(parsed.value["items"].as_array().size(), 2U);

    auto bad = JsonValue::parse("{ \"unterminated\": [1, 2");
    XWINE_EXPECT_FALSE(bad.ok);
}

XWINE_TEST(LoggerRingBufferAndFileSink) {
    auto dir = make_temp_test_dir("logger");
    xwine::platform::Logger log(128);
    log.set_min_level(xwine::platform::LogLevel::Debug);
    XWINE_EXPECT_TRUE(log.enable_file_sink(dir / "test.log"));

    log.trace("Sub", "should be filtered");
    log.info("Sub", "hello info");
    log.warn("Sub", "hello warn");
    log.error("Sub", "hello error");

    auto entries = log.recent_entries(10, xwine::platform::LogLevel::Trace);
    XWINE_EXPECT_EQ(entries.size(), 3U);
    XWINE_EXPECT_EQ(log.warn_count(), 1U);
    XWINE_EXPECT_EQ(log.error_count(), 1U);
    XWINE_EXPECT_TRUE(std::filesystem::exists(dir / "test.log"));
}

XWINE_TEST(ConfigValidationClampingAndPersistence) {
    auto dir = make_temp_test_dir("config");
    xwine::platform::ConfigManager cfg;
    cfg.reset_to_defaults(dir);
    cfg.config().input.left_stick_deadzone = 0.99f; // Out of range
    cfg.config().runtime.xbox_target_mode = "AppMode_1GB";

    auto val = cfg.validate_and_clamp();
    XWINE_EXPECT_FALSE(val.warnings.empty());
    XWINE_EXPECT_NEAR(cfg.config().input.left_stick_deadzone, 0.60f, 1e-4);
    XWINE_EXPECT_EQ(cfg.config().runtime.simulated_memory_cap_mib, 1024ULL);

    const auto cfg_path = dir / "config.json";
    XWINE_EXPECT_TRUE(cfg.save_to_file(cfg_path));

    xwine::platform::ConfigManager loaded;
    XWINE_EXPECT_TRUE(loaded.load_from_file(cfg_path));
    XWINE_EXPECT_EQ(loaded.config().runtime.xbox_target_mode, std::string("AppMode_1GB"));
    XWINE_EXPECT_EQ(loaded.config().runtime.simulated_memory_cap_mib, 1024ULL);
}

XWINE_TEST(PeInspectorParsesPE32PlusD3D12AndImports) {
    auto bytes = xwine::test::build_synthetic_pe(
        true,
        {
            {"KERNEL32.dll", {"CreateFileW", "VirtualAlloc"}},
            {"USER32.dll", {"CreateWindowExW"}},
            {"d3d12.dll", {"D3D12CreateDevice"}}
        },
        false, false);

    auto rep = xwine::runtime::PeInspector::inspect_bytes(bytes, "test_dx12.exe");
    XWINE_EXPECT_TRUE(rep.valid_pe);
    XWINE_EXPECT_EQ(static_cast<int>(rep.architecture), static_cast<int>(xwine::runtime::PeArchitecture::X86_64));
    XWINE_EXPECT_EQ(static_cast<int>(rep.format), static_cast<int>(xwine::runtime::PeFormat::PE32Plus));
    XWINE_EXPECT_EQ(rep.sections.size(), 2U);
    XWINE_EXPECT_EQ(rep.imports.size(), 3U);
    XWINE_EXPECT_EQ(rep.imports[2].dll_name, std::string("d3d12.dll"));
    XWINE_EXPECT_EQ(rep.imports[2].functions[0], std::string("D3D12CreateDevice"));
    XWINE_EXPECT_FALSE(rep.can_execute_locally);
    XWINE_EXPECT_FALSE(rep.verified_on_xbox_hardware);
    XWINE_EXPECT_FALSE(rep.xbox_blockers.empty());
}

XWINE_TEST(PeInspectorDetects32BitVulkanDrmAntiCheatAndRwx) {
    auto bytes = xwine::test::build_synthetic_pe(
        false,
        {
            {"vulkan-1.dll", {"vkCreateInstance"}},
            {"steam_api.dll", {"SteamAPI_Init"}},
            {"EasyAntiCheat_x86.dll", {"CreateGameClient"}}
        },
        true, true);

    auto rep = xwine::runtime::PeInspector::inspect_bytes(bytes, "blocked_sample.exe");
    XWINE_EXPECT_TRUE(rep.valid_pe);
    XWINE_EXPECT_EQ(static_cast<int>(rep.architecture), static_cast<int>(xwine::runtime::PeArchitecture::X86_32));
    XWINE_EXPECT_TRUE(rep.security.has_clr_dotnet_header);
    XWINE_EXPECT_TRUE(rep.xbox_blockers.size() >= 5U);
    XWINE_EXPECT_FALSE(rep.can_execute_locally);

    auto bad = xwine::runtime::PeInspector::inspect_bytes({0x00, 0x01, 0x02}, "truncated.exe");
    XWINE_EXPECT_FALSE(bad.valid_pe);
}

XWINE_TEST(VirtualMemoryEnforcesWxAndFiveGbCap) {
    using namespace xwine::runtime;
    VirtualMemoryBudgetModel mem(xwine::platform::kXboxDevModeGameRamCapBytes, true);

    auto rwx = mem.reserve_and_commit(0x10000, 4096, MemoryProtection::ExecuteReadWrite, ".text");
    XWINE_EXPECT_FALSE(rwx.allowed);

    auto rw = mem.reserve_and_commit(0x10000, 1024ULL * 1024ULL * 64ULL, MemoryProtection::ReadWrite, ".text");
    XWINE_EXPECT_TRUE(rw.allowed);

    auto rx = mem.protect_region(0x10000, MemoryProtection::ExecuteRead);
    XWINE_EXPECT_TRUE(rx.allowed);

    auto oom = mem.reserve_and_commit(0x20000000, 6ULL * 1024ULL * 1024ULL * 1024ULL,
                                      MemoryProtection::ReadWrite, "too_big");
    XWINE_EXPECT_FALSE(oom.allowed);
}

XWINE_TEST(VdfParserReadsValveAppManifest) {
    const std::string acf =
        "// Sample Valve AppManifest\n"
        "\"AppState\"\n{\n"
        "\t\"appid\"\t\t\"620\"\n"
        "\t\"name\"\t\t\"Portal 2\"\n"
        "\t\"installdir\"\t\"Portal 2\"\n"
        "\t\"SizeOnDisk\"\t\"12884901888\"\n"
        "}\n";
    auto res = xwine::steam::VdfParser::parse(acf);
    XWINE_EXPECT_TRUE(res.ok);
    const auto* st = res.root.find_child("AppState");
    XWINE_EXPECT_TRUE(st != nullptr);
    XWINE_EXPECT_EQ(st->child_value("appid"), std::string("620"));
    XWINE_EXPECT_EQ(st->child_value("name"), std::string("Portal 2"));

    auto bad = xwine::steam::VdfParser::parse("\"AppState\" { \"unclosed\" ");
    XWINE_EXPECT_FALSE(bad.ok);
}

XWINE_TEST(ControllerDeadzoneCurveRemappingAndXInputSnapshot) {
    using namespace xwine::input;
    // Inside deadzone -> zero
    auto zeroed = apply_radial_deadzone_and_curve(0.08f, 0.08f, 0.15f, 1.0f, false);
    XWINE_EXPECT_NEAR(zeroed.x, 0.0f, 1e-5);
    XWINE_EXPECT_NEAR(zeroed.y, 0.0f, 1e-5);

    // Full deflection -> magnitude 1.0
    auto full = apply_radial_deadzone_and_curve(1.0f, 0.0f, 0.15f, 1.0f, false);
    XWINE_EXPECT_NEAR(full.x, 1.0f, 1e-4);

    // Trigger threshold
    XWINE_EXPECT_NEAR(apply_trigger_threshold(0.05f, 0.10f), 0.0f, 1e-5);
    XWINE_EXPECT_NEAR(apply_trigger_threshold(1.0f, 0.10f), 1.0f, 1e-5);

    // Remapping + XInput conversion
    ControllerMapper mapper;
    mapper.apply_preset("SwappedConfirmCancel");
    XWINE_EXPECT_EQ(static_cast<int>(mapper.map_button(GamepadButton::A)),
                    static_cast<int>(GamepadButton::B));

    NormalizedGamepadState gs;
    gs.connected = true;
    gs.buttons[static_cast<size_t>(GamepadButton::A)] = true;
    gs.buttons[static_cast<size_t>(GamepadButton::LeftBumper)] = true;
    gs.left_trigger = 1.0f;
    gs.left_stick = {0.5f, -1.0f}; // SDL screen-up (-1) -> XInput +32767
    auto xi = mapper.to_xinput(gs);
    XWINE_EXPECT_TRUE((xi.wButtons & kXInputA) != 0);
    XWINE_EXPECT_TRUE((xi.wButtons & kXInputLeftShoulder) != 0);
    XWINE_EXPECT_EQ(xi.bLeftTrigger, 255);
    XWINE_EXPECT_EQ(xi.sThumbLY, 32767);
}

XWINE_TEST(LibraryManagerSandboxSecurityAndPersistence) {
    auto dir = make_temp_test_dir("library");
    xwine::runtime::LibraryManager lib;
    lib.set_sandbox_root(dir / "games");

    XWINE_EXPECT_TRUE(xwine::runtime::LibraryManager::is_path_within_root(dir / "games", dir / "games" / "titleA"));
    XWINE_EXPECT_FALSE(xwine::runtime::LibraryManager::is_path_within_root(dir / "games", dir / "games" / ".." / "escape"));

    xwine::runtime::GameEntry g;
    g.id = "test-01";
    g.title = "Test Title";
    g.installed = true;
    g.compatibility = xwine::runtime::CompatibilityRating::Verified_Xbox; // Must be downgraded by honesty guard!
    XWINE_EXPECT_TRUE(lib.add_or_update_game(g));

    auto* saved = lib.find_game("test-01");
    XWINE_EXPECT_TRUE(saved != nullptr);
    XWINE_EXPECT_TRUE(saved->compatibility != xwine::runtime::CompatibilityRating::Verified_Xbox);

    const auto save_file = dir / "library.json";
    XWINE_EXPECT_TRUE(lib.save_to_file(save_file));

    xwine::runtime::LibraryManager reloaded;
    XWINE_EXPECT_TRUE(reloaded.load_from_file(save_file));
    XWINE_EXPECT_EQ(reloaded.all_games().size(), 1U);
    XWINE_EXPECT_EQ(reloaded.all_games()[0].title, std::string("Test Title"));
}

XWINE_TEST(BenchmarkTrackerComputesPercentiles) {
    xwine::platform::BenchmarkTracker bm(100);
    for (int i = 1; i <= 100; ++i) {
        bm.record_frame_ms(static_cast<double>(i) * 0.2);
    }
    auto st = bm.frame_stats();
    XWINE_EXPECT_EQ(st.total_frames, 100ULL);
    XWINE_EXPECT_NEAR(st.min_frame_ms, 0.2, 1e-4);
    XWINE_EXPECT_NEAR(st.max_frame_ms, 20.0, 1e-4);
    XWINE_EXPECT_TRUE(st.p99_frame_ms >= st.p95_frame_ms);
    XWINE_EXPECT_TRUE(st.current_fps > 0.0);
}

int main() {
    return xwine::test::run_all_tests("XWine Core Unit Tests");
}
