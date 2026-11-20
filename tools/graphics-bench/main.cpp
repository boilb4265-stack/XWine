#include "xwine/graphics/graphics_backend.hpp"
#include "xwine/graphics/swapchain_bridge.hpp"
#include "xwine/platform/benchmark.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <iomanip>
#include <iostream>

// Standalone Phase 5 Graphics Capability & Offscreen Render Harness
// Independent from the XWine Launcher UI.
int main() {
    std::cout << "=== Project XWine — Standalone Graphics Verification Harness (Phase 5) ===\n";

    xwine::graphics::GraphicsCapabilityRegistry reg;
    for (const auto& route : reg.all_xbox_routes()) {
        std::cout << "  [" << std::setw(11) << std::left << xwine::graphics::to_string(route.guest_api)
                  << "] GameMode=" << (route.viable_on_xbox_uwp_game_mode ? "YES" : "NO ")
                  << " | Layers=" << route.estimated_translation_layers
                  << " | " << route.route_name << "\n";
    }

    // 1. Verify CoreWindow Swapchain Bridge & 5 GB UMA Budget Enforcement
    xwine::graphics::CoreWindowSwapchainBridge bridge(true);
    xwine::graphics::SwapchainDesc dx12_desc;
    dx12_desc.guest_hwnd = 0x10000;
    dx12_desc.width = 1920;
    dx12_desc.height = 1080;
    dx12_desc.buffer_count = 3;
    dx12_desc.guest_api = xwine::graphics::GraphicsApiId::Direct3D12;

    auto sc_res = bridge.create_swapchain_for_hwnd(dx12_desc, nullptr);
    if (!sc_res.ok) {
        std::cerr << "[FAIL] D3D12 CoreWindow swapchain creation failed: " << sc_res.status_message << "\n";
        return 1;
    }
    for (int f = 0; f < 60; ++f) bridge.present(sc_res.swapchain_id);

    // Ensure Vulkan swapchain is rejected honestly on Xbox target
    xwine::graphics::SwapchainDesc vk_desc = dx12_desc;
    vk_desc.guest_api = xwine::graphics::GraphicsApiId::Vulkan;
    auto vk_res = bridge.create_swapchain_for_hwnd(vk_desc, nullptr);
    if (vk_res.ok) {
        std::cerr << "[FAIL] Vulkan swapchain should be rejected on Xbox UWP.\n";
        return 1;
    }

    // Ensure 5 GB UMA cap prevents OOM allocations
    bool big_ok = bridge.allocate_gpu_resource(999, 6ULL * 1024ULL * 1024ULL * 1024ULL, "Oversized_Texture");
    if (big_ok) {
        std::cerr << "[FAIL] UMA budget failed to block 6 GB allocation under 5 GB Xbox Dev Mode cap.\n";
        return 1;
    }

    auto mem = bridge.memory_snapshot();
    std::cout << "\nCoreWindow Swapchain Bridge: OK (" << bridge.metrics(sc_res.swapchain_id).present_count
              << " presents, UMA utilization: " << std::fixed << std::setprecision(2)
              << mem.utilization_percent << "%)\n";

    // 2. Host Hardware Renderer Offscreen Frame-Pacing Smoke Test (SDL3 Metal on macOS / D3D11-12 on Win/Xbox)
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "[WARN] SDL_Init(VIDEO) unavailable in headless environment: " << SDL_GetError() << "\n";
        return 0;
    }

    SDL_Window* win = SDL_CreateWindow("XWine Gfx Bench", 640, 360, SDL_WINDOW_HIDDEN);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
    if (!win || !ren) {
        std::cerr << "[WARN] Offscreen SDL3 renderer unavailable: " << SDL_GetError() << "\n";
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    const char* driver = SDL_GetRendererName(ren);
    xwine::platform::BenchmarkTracker bm(120);
    constexpr int kFrames = 120;
    const SDL_Vertex tri[3] = {
        {{320.0f,  40.0f}, {0.13f, 0.83f, 0.93f, 1.0f}, {0.5f, 0.0f}},
        {{560.0f, 320.0f}, {0.05f, 0.65f, 0.91f, 1.0f}, {1.0f, 1.0f}},
        {{ 80.0f, 320.0f}, {0.95f, 0.96f, 0.98f, 1.0f}, {0.0f, 1.0f}}
    };

    for (int f = 0; f < kFrames; ++f) {
        auto t0 = std::chrono::steady_clock::now();
        SDL_SetRenderDrawColor(ren, 12, 15, 20, 255);
        SDL_RenderClear(ren);
        SDL_RenderGeometry(ren, nullptr, tri, 3, nullptr, 0);
        SDL_RenderPresent(ren);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bm.record_frame_ms(ms);
    }

    auto st = bm.frame_stats();
    std::cout << "Host SDL3 Renderer (" << (driver ? driver : "unknown") << ") " << kFrames
              << " frames: avg=" << std::setprecision(3) << st.avg_frame_ms
              << " ms, p99=" << st.p99_frame_ms << " ms\n";

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
