#include "xwine/graphics/graphics_backend.hpp"

namespace xwine::graphics {

namespace {
class VulkanAssessor final : public IGraphicsApiTranslator {
public:
    GraphicsApiId guest_api() const override { return GraphicsApiId::Vulkan; }
    GraphicsRouteDescriptor describe_xbox_route() const override {
        GraphicsRouteDescriptor d;
        d.guest_api = GraphicsApiId::Vulkan;
        d.route_name = "Vulkan Unavailable Natively (Experimental Mesa Dozen Fallback Only)";
        d.xbox_target_backend = "None (No Xbox Vulkan ICD)";
        d.status = platform::CapabilityStatus::TechnicallyUnsupported;
        d.viable_on_xbox_uwp_game_mode = false;
        d.viable_on_xbox_uwp_app_mode = false;
        d.estimated_translation_layers = 2;
        d.shader_plan = describe_shader_pipeline(GraphicsApiId::Vulkan);
        d.swapchain_strategy = "Requires custom VK_KHR_surface -> CoreWindow integration if Mesa Dozen is ever ported.";
        d.dxvk_vkd3d_note = "Xbox OS ships no Vulkan ICD; DXVK/VKD3D-Proton cannot run directly on Xbox hardware.";
        return d;
    }
};
} // namespace

std::unique_ptr<IGraphicsApiTranslator> create_vulkan_assessor() {
    return std::make_unique<VulkanAssessor>();
}

} // namespace xwine::graphics
