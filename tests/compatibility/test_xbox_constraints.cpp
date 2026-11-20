#include "../unit/test_framework.hpp"

#include "xwine/graphics/graphics_backend.hpp"
#include "xwine/platform/platform.hpp"
#include "xwine/platform/xbox_capabilities.hpp"
#include "xwine/runtime/runtime_manager.hpp"

XWINE_TEST(XboxHardwareProfileNeverClaimsUnverifiedHardware) {
    auto game_profile = xwine::platform::get_xbox_series_s_hardware_profile(true);
    auto app_profile = xwine::platform::get_xbox_series_s_hardware_profile(false);

    XWINE_EXPECT_FALSE(game_profile.hardware_verified);
    XWINE_EXPECT_FALSE(app_profile.hardware_verified);
    XWINE_EXPECT_EQ(game_profile.app_usable_ram_bytes, xwine::platform::kXboxDevModeGameRamCapBytes);
    XWINE_EXPECT_EQ(app_profile.app_usable_ram_bytes, xwine::platform::kXboxDevModeAppRamCapBytes);
    XWINE_EXPECT_TRUE(game_profile.app_usable_ram_bytes < game_profile.physical_ram_bytes);
}

XWINE_TEST(GraphicsMatrixCorrectlyFlagsVulkanUnsupportedAndD3D12Native) {
    xwine::graphics::GraphicsCapabilityRegistry gfx;
    auto vk = gfx.route_for(xwine::graphics::GraphicsApiId::Vulkan);
    XWINE_EXPECT_EQ(static_cast<int>(vk.status),
                    static_cast<int>(xwine::platform::CapabilityStatus::TechnicallyUnsupported));
    XWINE_EXPECT_FALSE(vk.viable_on_xbox_uwp_game_mode);

    auto d3d12 = gfx.route_for(xwine::graphics::GraphicsApiId::Direct3D12);
    XWINE_EXPECT_TRUE(d3d12.viable_on_xbox_uwp_game_mode);
    XWINE_EXPECT_FALSE(d3d12.viable_on_xbox_uwp_app_mode);
    XWINE_EXPECT_EQ(d3d12.estimated_translation_layers, 0U);

    // Budget checks
    XWINE_EXPECT_TRUE(xwine::graphics::GraphicsCapabilityRegistry::fits_xbox_uma_budget(
        3ULL * 1024ULL * 1024ULL * 1024ULL, true));
    XWINE_EXPECT_FALSE(xwine::graphics::GraphicsCapabilityRegistry::fits_xbox_uma_budget(
        6ULL * 1024ULL * 1024ULL * 1024ULL, true));
    XWINE_EXPECT_FALSE(xwine::graphics::GraphicsCapabilityRegistry::fits_xbox_uma_budget(
        900ULL * 1024ULL * 1024ULL, false));
}

XWINE_TEST(RuntimeManagerMarksUpstreamWineAndProtonUnsupportedOnXboxUwp) {
    xwine::runtime::RuntimeManager rt;
    bool found_wine_unsupported = false;
    for (const auto& b : rt.backends()) {
        if (b.id == "wine-upstream") {
            XWINE_EXPECT_FALSE(b.supports_xbox_uwp);
            XWINE_EXPECT_EQ(static_cast<int>(b.status),
                            static_cast<int>(xwine::platform::CapabilityStatus::TechnicallyUnsupported));
            found_wine_unsupported = true;
        }
    }
    XWINE_EXPECT_TRUE(found_wine_unsupported);
}

int main() {
    return xwine::test::run_all_tests("XWine Xbox Constraints & Honesty Guard Tests");
}
