#include "xwine/graphics/graphics_backend.hpp"

namespace xwine::graphics {

namespace {
class D3D11Translator final : public IGraphicsApiTranslator {
public:
    GraphicsApiId guest_api() const override { return GraphicsApiId::Direct3D11; }
    GraphicsRouteDescriptor describe_xbox_route() const override {
        GraphicsRouteDescriptor d;
        d.guest_api = GraphicsApiId::Direct3D11;
        d.route_name = "D3D11 Native Pass-Through (Xbox OS D3D11On12) + DXGI Shim";
        d.xbox_target_backend = "Direct3D 11 (UWP Game Mode)";
        d.status = platform::CapabilityStatus::Planned;
        d.viable_on_xbox_uwp_game_mode = true;
        d.viable_on_xbox_uwp_app_mode = true; // FL 10_0 only
        d.estimated_translation_layers = 0;
        d.shader_plan = describe_shader_pipeline(GraphicsApiId::Direct3D11);
        d.swapchain_strategy = "Wrap IDXGIFactory2::CreateSwapChainForHwnd -> CreateSwapChainForCoreWindow.";
        d.dxvk_vkd3d_note = "DXVK is unnecessary and harmful on Xbox because D3D11 is exposed natively by Xbox UWP.";
        return d;
    }
};
} // namespace

std::unique_ptr<IGraphicsApiTranslator> create_d3d11_translator() {
    return std::make_unique<D3D11Translator>();
}

} // namespace xwine::graphics
