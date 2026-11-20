#include "xwine/graphics/graphics_backend.hpp"

namespace xwine::graphics {

ShaderTranslationPlan describe_shader_pipeline(GraphicsApiId api) {
    switch (api) {
        case GraphicsApiId::Direct3D12:
            return {"DXIL / DXBC (SM 5.1 - 6.x)", "Native Xbox RDNA 2 DXIL", false, false,
                    "Direct shader consumption by native Xbox UWP D3D12 driver."};
        case GraphicsApiId::Direct3D11:
            return {"DXBC (SM 5.0)", "Native Xbox UWP D3D11 (D3D11On12)", false, false,
                    "Handled natively by Xbox OS D3D11On12 shader translation."};
        case GraphicsApiId::Direct3D10:
            return {"DXBC (SM 4.0/4.1)", "Xbox UWP D3D11", false, false,
                    "SM 4.x bytecode is directly accepted by D3D11."};
        case GraphicsApiId::Direct3D9:
            return {"D3D9 Shader Bytecode (SM 2.0/3.0)", "DXIL via Microsoft D3D9On12", true, false,
                    "Requires D3D9On12 shader translator and PSO caching within 5 GB UMA cap."};
        case GraphicsApiId::Vulkan:
            return {"SPIR-V", "NIR -> DXIL (Mesa Dozen)", true, true,
                    "No native Vulkan ICD on Xbox; SPIR-V->NIR->DXIL incurs heavy compile stutter and RAM cost."};
        case GraphicsApiId::OpenGL:
            return {"GLSL", "HLSL / DXIL (ANGLE or Mesa Gallium D3D12)", true, false,
                    "Requires UWP port of ANGLE (GLES) or Mesa D3D12 Gallium backend."};
        case GraphicsApiId::Metal:
            return {"MSL", "Apple Silicon Metal", false, false,
                    "Used exclusively on macOS development workstation for XWine Launcher UI."};
    }
    return {"Unknown", "Unsupported", true, false, "Unmapped shader IR."};
}

} // namespace xwine::graphics
