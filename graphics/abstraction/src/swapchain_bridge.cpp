#include "xwine/graphics/swapchain_bridge.hpp"
#include "xwine/platform/logger.hpp"
#include "xwine/platform/xbox_capabilities.hpp"

namespace xwine::graphics {

CoreWindowSwapchainBridge::CoreWindowSwapchainBridge(bool xbox_game_mode)
    : game_mode_(xbox_game_mode) {
    total_budget_bytes_ =
        xbox_game_mode ? platform::kXboxDevModeGameRamCapBytes : platform::kXboxDevModeAppRamCapBytes;
    runtime_reserved_bytes_ =
        xbox_game_mode ? (512ULL * 1024ULL * 1024ULL) : (256ULL * 1024ULL * 1024ULL);
}

CoreWindowSwapchainBridge::CreateResult CoreWindowSwapchainBridge::create_swapchain_for_hwnd(
    const SwapchainDesc& desc, void* native_core_window) {
    if (desc.width < 64 || desc.height < 64 || desc.width > 7680 || desc.height > 4320) {
        return {false, 0, "Invalid swapchain dimensions."};
    }
    if (desc.buffer_count < 2 || desc.buffer_count > 4) {
        return {false, 0, "UWP DXGI FLIP_DISCARD requires 2..4 swapchain buffers."};
    }

    const auto route = registry_.route_for(desc.guest_api);
    if (desc.guest_api == GraphicsApiId::Vulkan) {
        return {false, 0,
                "Rejected Vulkan swapchain: Xbox OS provides no native Vulkan ICD (VK_KHR_surface unavailable)."};
    }
    if (!game_mode_ && !route.viable_on_xbox_uwp_app_mode) {
        return {false, 0,
                "Requested API (" + to_string(desc.guest_api) +
                    ") requires Xbox Dev Mode UWP Game Mode (switch App -> Game in Xbox Device Portal)."};
    }

    // RGBA8 (4 bytes/px) * buffer_count
    const uint64_t backbuffer_bytes =
        static_cast<uint64_t>(desc.width) * static_cast<uint64_t>(desc.height) * 4ULL * desc.buffer_count;

    const uint64_t id = next_id_++;
    const uint64_t resource_key = 0xFFFF0000ULL + id;
    if (!allocate_gpu_resource(resource_key, backbuffer_bytes, "DXGI_CoreWindow_Swapchain")) {
        return {false, 0, "Swapchain backbuffer allocation exceeded Xbox Dev Mode UMA memory ceiling."};
    }

    InternalSwapchain sc;
    sc.desc = desc;
    sc.core_window = native_core_window;
    sc.metrics.swapchain_id = id;
    sc.metrics.estimated_backbuffer_bytes = backbuffer_bytes;
    sc.metrics.corewindow_bound = (native_core_window != nullptr);
    sc.metrics.active_route = route.route_name;
    swapchains_[id] = std::move(sc);

    platform::Logger::instance().info(
        "Graphics",
        "Created CoreWindow-bridged swapchain #" + std::to_string(id) + " (" +
            std::to_string(desc.width) + "x" + std::to_string(desc.height) + ", " +
            to_string(desc.guest_api) + ")");

    return {true, id, "Bridged HWND swapchain onto CoreWindow (" + route.route_name + ")."};
}

bool CoreWindowSwapchainBridge::present(uint64_t swapchain_id) {
    auto it = swapchains_.find(swapchain_id);
    if (it == swapchains_.end()) return false;
    ++it->second.metrics.present_count;
    return true;
}

bool CoreWindowSwapchainBridge::resize(uint64_t swapchain_id, uint32_t width, uint32_t height) {
    if (width < 64 || height < 64 || width > 7680 || height > 4320) return false;
    auto it = swapchains_.find(swapchain_id);
    if (it == swapchains_.end()) return false;

    const uint64_t resource_key = 0xFFFF0000ULL + swapchain_id;
    const uint64_t new_bytes =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ULL * it->second.desc.buffer_count;
    release_gpu_resource(resource_key);
    if (!allocate_gpu_resource(resource_key, new_bytes, "DXGI_CoreWindow_Swapchain_Resized")) {
        return false;
    }
    it->second.desc.width = width;
    it->second.desc.height = height;
    it->second.metrics.estimated_backbuffer_bytes = new_bytes;
    return true;
}

bool CoreWindowSwapchainBridge::destroy_swapchain(uint64_t swapchain_id) {
    auto it = swapchains_.find(swapchain_id);
    if (it == swapchains_.end()) return false;
    release_gpu_resource(0xFFFF0000ULL + swapchain_id);
    swapchains_.erase(it);
    return true;
}

bool CoreWindowSwapchainBridge::allocate_gpu_resource(uint64_t resource_id, uint64_t size_bytes,
                                                      const std::string& /*tag*/) {
    const uint64_t usable = total_budget_bytes_ > runtime_reserved_bytes_
                                ? (total_budget_bytes_ - runtime_reserved_bytes_)
                                : 0;
    if (allocated_gpu_bytes_ + size_bytes > usable) {
        return false;
    }
    gpu_allocations_[resource_id] = size_bytes;
    allocated_gpu_bytes_ += size_bytes;
    return true;
}

bool CoreWindowSwapchainBridge::release_gpu_resource(uint64_t resource_id) {
    auto it = gpu_allocations_.find(resource_id);
    if (it == gpu_allocations_.end()) return false;
    allocated_gpu_bytes_ = (allocated_gpu_bytes_ >= it->second) ? (allocated_gpu_bytes_ - it->second) : 0;
    gpu_allocations_.erase(it);
    return true;
}

UmaMemorySnapshot CoreWindowSwapchainBridge::memory_snapshot() const {
    UmaMemorySnapshot snap;
    snap.total_budget_bytes = total_budget_bytes_;
    snap.runtime_reserved_bytes = runtime_reserved_bytes_;
    snap.allocated_gpu_bytes = allocated_gpu_bytes_;
    const uint64_t used = runtime_reserved_bytes_ + allocated_gpu_bytes_;
    snap.remaining_bytes = (used >= total_budget_bytes_) ? 0 : (total_budget_bytes_ - used);
    snap.utilization_percent =
        total_budget_bytes_ > 0
            ? (static_cast<double>(used) / static_cast<double>(total_budget_bytes_)) * 100.0
            : 100.0;
    snap.high_pressure_warning = snap.utilization_percent >= 80.0;
    return snap;
}

SwapchainMetrics CoreWindowSwapchainBridge::metrics(uint64_t swapchain_id) const {
    auto it = swapchains_.find(swapchain_id);
    return it == swapchains_.end() ? SwapchainMetrics{} : it->second.metrics;
}

} // namespace xwine::graphics
