#include "xwine/graphics/graphics_backend.hpp"

namespace xwine::graphics {

namespace {
class D3D10Translator final : public IGraphicsApiTranslator {
public:
    GraphicsApiId guest_api() const override { return GraphicsApiId::Direct3D10; }
    GraphicsRouteDescriptor describe_xbox_route() const override {
        GraphicsRouteDescriptor d;
        d.guest_api = GraphicsApiId::Direct3D10;
        d.route_name = "D3D10 -> Thin COM Adapter -> Native Xbox UWP D3D11";
        d.xbox_target_backend = "Direct3D 11 (OS D3D11On12)";
        d.status = platform::CapabilityStatus::Planned;
        d.viable_on_xbox_uwp_game_mode = true;
        d.viable_on_xbox_uwp_app_mode = false;
        d.estimated_translation_layers = 1;
        d.shader_plan = describe_shader_pipeline(GraphicsApiId::Direct3D10);
        d.swapchain_strategy = "Translate CreateSwapChain HWND to CreateSwapChainForCoreWindow.";
        d.dxvk_vkd3d_note = "Avoids DXVK Vulkan dependency.";
        return d;
    }
};
} // namespace

std::unique_ptr<IGraphicsApiTranslator> create_d3d10_translator() {
    return std::make_unique<D3D10Translator>();
}

} // namespace xwine::graphics
