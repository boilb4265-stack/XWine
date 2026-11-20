#include "xwine/graphics/graphics_backend.hpp"
#include "xwine/platform/xbox_capabilities.hpp"

namespace xwine::graphics {

GraphicsCapabilityRegistry::GraphicsCapabilityRegistry() {
    translators_.push_back(create_d3d12_translator());
    translators_.push_back(create_d3d11_translator());
    translators_.push_back(create_d3d10_translator());
    translators_.push_back(create_d3d9_translator());
    translators_.push_back(create_vulkan_assessor());
}

std::vector<GraphicsRouteDescriptor> GraphicsCapabilityRegistry::all_xbox_routes() const {
    std::vector<GraphicsRouteDescriptor> out;
    out.reserve(translators_.size());
    for (const auto& t : translators_) {
        out.push_back(t->describe_xbox_route());
    }
    return out;
}

GraphicsRouteDescriptor GraphicsCapabilityRegistry::route_for(GraphicsApiId api) const {
    for (const auto& t : translators_) {
        if (t->guest_api() == api) {
            return t->describe_xbox_route();
        }
    }
    GraphicsRouteDescriptor fallback;
    fallback.guest_api = api;
    fallback.route_name = "Unsupported / Unmapped Graphics API";
    fallback.status = platform::CapabilityStatus::TechnicallyUnsupported;
    fallback.shader_plan = describe_shader_pipeline(api);
    return fallback;
}

GraphicsRouteDescriptor GraphicsCapabilityRegistry::route_for_detected(
    runtime::DetectedGraphicsApi detected) const {
    switch (detected) {
        case runtime::DetectedGraphicsApi::Direct3D12: return route_for(GraphicsApiId::Direct3D12);
        case runtime::DetectedGraphicsApi::Direct3D11: return route_for(GraphicsApiId::Direct3D11);
        case runtime::DetectedGraphicsApi::Direct3D10: return route_for(GraphicsApiId::Direct3D10);
        case runtime::DetectedGraphicsApi::Direct3D9:  return route_for(GraphicsApiId::Direct3D9);
        case runtime::DetectedGraphicsApi::Vulkan:     return route_for(GraphicsApiId::Vulkan);
        case runtime::DetectedGraphicsApi::OpenGL:     return route_for(GraphicsApiId::OpenGL);
        case runtime::DetectedGraphicsApi::NoneDetected: break;
    }
    GraphicsRouteDescriptor sw;
    sw.guest_api = GraphicsApiId::Direct3D11;
    sw.route_name = "Software / Console Output (No 3D API Imported)";
    sw.xbox_target_backend = "XWine Virtual Console / Software Surface -> D3D11";
    sw.status = platform::CapabilityStatus::Planned;
    sw.viable_on_xbox_uwp_game_mode = true;
    sw.viable_on_xbox_uwp_app_mode = true;
    return sw;
}

bool GraphicsCapabilityRegistry::fits_xbox_uma_budget(uint64_t estimated_vram_plus_ram_bytes, bool game_mode) {
    const uint64_t cap = game_mode ? platform::kXboxDevModeGameRamCapBytes : platform::kXboxDevModeAppRamCapBytes;
    // Reserve 512 MiB for launcher/runtime overhead when in Game Mode, 256 MiB in App Mode
    const uint64_t overhead = game_mode ? (512ULL * 1024ULL * 1024ULL) : (256ULL * 1024ULL * 1024ULL);
    if (cap <= overhead) return false;
    return estimated_vram_plus_ram_bytes <= (cap - overhead);
}

} // namespace xwine::graphics
