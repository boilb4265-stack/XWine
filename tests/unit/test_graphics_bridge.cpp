#include "test_framework.hpp"

#include "xwine/graphics/swapchain_bridge.hpp"

XWINE_TEST(CoreWindowSwapchainBridgeValidatesD3D12AppVsGameAndUmaBudget) {
    using namespace xwine::graphics;

    // 1. Game Mode (5,120 MiB cap): D3D12 & D3D11 succeed, Vulkan rejected
    CoreWindowSwapchainBridge game_bridge(true);
    SwapchainDesc d12;
    d12.guest_hwnd = 0x10000;
    d12.width = 1920;
    d12.height = 1080;
    d12.buffer_count = 3;
    d12.guest_api = GraphicsApiId::Direct3D12;

    auto res = game_bridge.create_swapchain_for_hwnd(d12, nullptr);
    XWINE_EXPECT_TRUE(res.ok);
    XWINE_EXPECT_EQ(game_bridge.active_swapchain_count(), 1U);
    XWINE_EXPECT_TRUE(game_bridge.present(res.swapchain_id));
    XWINE_EXPECT_EQ(game_bridge.metrics(res.swapchain_id).present_count, 1ULL);

    // Resize to 4K updates backbuffer accounting
    XWINE_EXPECT_TRUE(game_bridge.resize(res.swapchain_id, 3840, 2160));
    XWINE_EXPECT_EQ(game_bridge.metrics(res.swapchain_id).estimated_backbuffer_bytes,
                    3840ULL * 2160ULL * 4ULL * 3ULL);

    // Vulkan must be rejected on Xbox UWP
    SwapchainDesc vk = d12;
    vk.guest_api = GraphicsApiId::Vulkan;
    XWINE_EXPECT_FALSE(game_bridge.create_swapchain_for_hwnd(vk, nullptr).ok);

    // High UMA pressure warning triggers >= 80% of 5 GB cap, OOM rejects > cap
    XWINE_EXPECT_TRUE(game_bridge.allocate_gpu_resource(1, 3700ULL * 1024ULL * 1024ULL, "Textures"));
    XWINE_EXPECT_TRUE(game_bridge.memory_snapshot().high_pressure_warning);
    XWINE_EXPECT_FALSE(game_bridge.allocate_gpu_resource(2, 2000ULL * 1024ULL * 1024ULL, "Overflow"));
    XWINE_EXPECT_TRUE(game_bridge.release_gpu_resource(1));
    XWINE_EXPECT_TRUE(game_bridge.destroy_swapchain(res.swapchain_id));
    XWINE_EXPECT_EQ(game_bridge.active_swapchain_count(), 0U);

    // 2. App Mode (1,024 MiB cap): D3D12 rejected honestly, D3D11 FL10_0 permitted
    CoreWindowSwapchainBridge app_bridge(false);
    XWINE_EXPECT_FALSE(app_bridge.create_swapchain_for_hwnd(d12, nullptr).ok);
    SwapchainDesc d11 = d12;
    d11.guest_api = GraphicsApiId::Direct3D11;
    XWINE_EXPECT_TRUE(app_bridge.create_swapchain_for_hwnd(d11, nullptr).ok);
}

int main() {
    return xwine::test::run_all_tests("XWine Phase 5 Graphics Swapchain Bridge Tests");
}
