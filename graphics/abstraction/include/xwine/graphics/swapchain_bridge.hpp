#pragma once

#include "xwine/graphics/graphics_backend.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xwine::graphics {

struct SwapchainDesc {
    uint64_t guest_hwnd = 0;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t buffer_count = 2; // Double/triple buffered FLIP_DISCARD
    bool vsync = true;
    GraphicsApiId guest_api = GraphicsApiId::Direct3D12;
};

struct SwapchainMetrics {
    uint64_t swapchain_id = 0;
    uint64_t present_count = 0;
    uint64_t estimated_backbuffer_bytes = 0;
    bool corewindow_bound = false;
    std::string active_route;
};

struct UmaMemorySnapshot {
    uint64_t total_budget_bytes = 0;
    uint64_t runtime_reserved_bytes = 0;
    uint64_t allocated_gpu_bytes = 0;
    uint64_t remaining_bytes = 0;
    double utilization_percent = 0.0;
    bool high_pressure_warning = false;
};

// Bridges desktop DXGI HWND swapchain requests onto Xbox UWP CoreWindow semantics
// and enforces the 5,120 MiB (Game Mode) / 1,024 MiB (App Mode) unified memory cap.
class CoreWindowSwapchainBridge {
public:
    explicit CoreWindowSwapchainBridge(bool xbox_game_mode = true);

    struct CreateResult {
        bool ok = false;
        uint64_t swapchain_id = 0;
        std::string status_message;
    };

    CreateResult create_swapchain_for_hwnd(const SwapchainDesc& desc, void* native_core_window = nullptr);
    bool present(uint64_t swapchain_id);
    bool resize(uint64_t swapchain_id, uint32_t width, uint32_t height);
    bool destroy_swapchain(uint64_t swapchain_id);

    // Unified Memory Architecture (UMA) resource tracking
    bool allocate_gpu_resource(uint64_t resource_id, uint64_t size_bytes, const std::string& tag);
    bool release_gpu_resource(uint64_t resource_id);

    UmaMemorySnapshot memory_snapshot() const;
    SwapchainMetrics metrics(uint64_t swapchain_id) const;
    size_t active_swapchain_count() const { return swapchains_.size(); }

private:
    struct InternalSwapchain {
        SwapchainDesc desc;
        SwapchainMetrics metrics;
        void* core_window = nullptr;
    };

    bool game_mode_ = true;
    uint64_t total_budget_bytes_ = 0;
    uint64_t runtime_reserved_bytes_ = 0;
    uint64_t allocated_gpu_bytes_ = 0;
    uint64_t next_id_ = 1;
    std::map<uint64_t, InternalSwapchain> swapchains_;
    std::map<uint64_t, uint64_t> gpu_allocations_;
    GraphicsCapabilityRegistry registry_;
};

} // namespace xwine::graphics
