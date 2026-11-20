#include "xwine/graphics/graphics_backend.hpp"

namespace xwine::graphics {

namespace {
class D3D9Translator final : public IGraphicsApiTranslator {
public:
    GraphicsApiId guest_api() const override { return GraphicsApiId::Direct3D9; }
    GraphicsRouteDescriptor describe_xbox_route() const override {
        GraphicsRouteDescriptor d;
        d.guest_api = GraphicsApiId::Direct3D9;
        d.route_name = "D3D9 -> Microsoft D3D9On12 -> Native Xbox D3D12";
        d.xbox_target_backend = "Direct3D 12 (via D3D9On12)";
        d.status = platform::CapabilityStatus::Planned;
        d.viable_on_xbox_uwp_game_mode = true;
        d.viable_on_xbox_uwp_app_mode = false;
        d.estimated_translation_layers = 1;
        d.shader_plan = describe_shader_pipeline(GraphicsApiId::Direct3D9);
        d.swapchain_strategy = "Map IDirect3DDevice9::Present to CoreWindow DXGI swapchain.";
        d.dxvk_vkd3d_note = "Avoids DXVK (which requires Vulkan absent on Xbox).";
        return d;
    }
};
} // namespace

std::unique_ptr<IGraphicsApiTranslator> create_d3d9_translator() {
    return std::make_unique<D3D9Translator>();
}

} // namespace xwine::graphics
