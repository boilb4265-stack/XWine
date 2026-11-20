#pragma once

#include "xwine/platform/types.hpp"
#include "xwine/runtime/pe_inspector.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xwine::graphics {

enum class GraphicsApiId {
    Direct3D9,
    Direct3D10,
    Direct3D11,
    Direct3D12,
    Vulkan,
    OpenGL,
    Metal
};

inline std::string to_string(GraphicsApiId id) {
    switch (id) {
        case GraphicsApiId::Direct3D9:  return "Direct3D 9";
        case GraphicsApiId::Direct3D10: return "Direct3D 10";
        case GraphicsApiId::Direct3D11: return "Direct3D 11";
        case GraphicsApiId::Direct3D12: return "Direct3D 12";
        case GraphicsApiId::Vulkan:     return "Vulkan";
        case GraphicsApiId::OpenGL:     return "OpenGL";
        case GraphicsApiId::Metal:      return "Metal";
    }
    return "Unknown";
}

struct ShaderTranslationPlan {
    std::string source_ir;       // e.g., "DXBC (SM 5.0)", "DXIL (SM 6.x)", "DX9 Bytecode", "SPIR-V"
    std::string target_ir;       // e.g., "Native Xbox DXIL/DXBC"
    bool requires_recompilation = false;
    bool requires_vulkan_icd = false;
    std::string notes;
};

struct GraphicsRouteDescriptor {
    GraphicsApiId guest_api = GraphicsApiId::Direct3D12;
    std::string route_name;
    std::string xbox_target_backend;
    platform::CapabilityStatus status = platform::CapabilityStatus::Planned;
    bool viable_on_xbox_uwp_game_mode = false;
    bool viable_on_xbox_uwp_app_mode = false;
    uint32_t estimated_translation_layers = 0;
    ShaderTranslationPlan shader_plan;
    std::string swapchain_strategy;
    std::string dxvk_vkd3d_note;
};

class IGraphicsApiTranslator {
public:
    virtual ~IGraphicsApiTranslator() = default;
    virtual GraphicsApiId guest_api() const = 0;
    virtual GraphicsRouteDescriptor describe_xbox_route() const = 0;
};

// Factory hooks implemented per API module (d3d9, d3d10, d3d11, d3d12, vulkan).
std::unique_ptr<IGraphicsApiTranslator> create_d3d9_translator();
std::unique_ptr<IGraphicsApiTranslator> create_d3d10_translator();
std::unique_ptr<IGraphicsApiTranslator> create_d3d11_translator();
std::unique_ptr<IGraphicsApiTranslator> create_d3d12_translator();
std::unique_ptr<IGraphicsApiTranslator> create_vulkan_assessor();
ShaderTranslationPlan describe_shader_pipeline(GraphicsApiId api);

class GraphicsCapabilityRegistry {
public:
    GraphicsCapabilityRegistry();

    std::vector<GraphicsRouteDescriptor> all_xbox_routes() const;
    GraphicsRouteDescriptor route_for(GraphicsApiId api) const;
    GraphicsRouteDescriptor route_for_detected(runtime::DetectedGraphicsApi detected) const;

    // Evaluates whether a target memory estimate fits inside Xbox Dev Mode UMA budget.
    static bool fits_xbox_uma_budget(uint64_t estimated_vram_plus_ram_bytes, bool game_mode);

private:
    std::vector<std::unique_ptr<IGraphicsApiTranslator>> translators_;
};

} // namespace xwine::graphics
