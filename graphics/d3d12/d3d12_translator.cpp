#include "xwine/graphics/graphics_backend.hpp"

namespace xwine::graphics {

namespace {
class D3D12Translator final : public IGraphicsApiTranslator {
public:
    GraphicsApiId guest_api() const override { return GraphicsApiId::Direct3D12; }
    GraphicsRouteDescriptor describe_xbox_route() const override {
        GraphicsRouteDescriptor d;
        d.guest_api = GraphicsApiId::Direct3D12;
        d.route_name = "D3D12 Direct Native Pass-Through + CoreWindow DXGI Shim";
        d.xbox_target_backend = "Direct3D 12 (Native RDNA 2 Driver)";
        d.status = platform::CapabilityStatus::Planned;
        d.viable_on_xbox_uwp_game_mode = true;
        d.viable_on_xbox_uwp_app_mode = false;
        d.estimated_translation_layers = 0;
        d.shader_plan = describe_shader_pipeline(GraphicsApiId::Direct3D12);
        d.swapchain_strategy = "Intercept DXGI HWND creation & fullscreen transitions; bind to UWP CoreWindow.";
        d.dxvk_vkd3d_note = "VKD3D-Proton translates D3D12->Vulkan and MUST NOT be used on Xbox where D3D12 is native.";
        return d;
    }
};
} // namespace

std::unique_ptr<IGraphicsApiTranslator> create_d3d12_translator() {
    return std::make_unique<D3D12Translator>();
}

} // namespace xwine::graphics
