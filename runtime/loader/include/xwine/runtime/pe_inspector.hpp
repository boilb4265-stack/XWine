#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xwine::runtime {

enum class PeArchitecture {
    Unknown,
    X86_32,
    X86_64,
    ARM64,
    ARM32
};

enum class PeFormat {
    Unknown,
    PE32,
    PE32Plus
};

enum class PeSubsystem {
    Unknown,
    Native,
    WindowsGui,
    WindowsConsole,
    EfiApplication,
    Xbox
};

enum class DetectedGraphicsApi {
    NoneDetected,
    Direct3D12,
    Direct3D11,
    Direct3D10,
    Direct3D9,
    Vulkan,
    OpenGL
};

inline std::string to_string(PeArchitecture arch) {
    switch (arch) {
        case PeArchitecture::X86_32: return "x86 (32-bit)";
        case PeArchitecture::X86_64: return "x86-64 (64-bit)";
        case PeArchitecture::ARM64:  return "ARM64";
        case PeArchitecture::ARM32:  return "ARM32";
        case PeArchitecture::Unknown: return "Unknown";
    }
    return "Unknown";
}

inline std::string to_string(PeFormat fmt) {
    switch (fmt) {
        case PeFormat::PE32: return "PE32";
        case PeFormat::PE32Plus: return "PE32+";
        case PeFormat::Unknown: return "Unknown";
    }
    return "Unknown";
}

inline std::string to_string(PeSubsystem sub) {
    switch (sub) {
        case PeSubsystem::Native: return "Native";
        case PeSubsystem::WindowsGui: return "Windows GUI";
        case PeSubsystem::WindowsConsole: return "Windows Console";
        case PeSubsystem::EfiApplication: return "EFI Application";
        case PeSubsystem::Xbox: return "Xbox";
        case PeSubsystem::Unknown: return "Unknown";
    }
    return "Unknown";
}

inline std::string to_string(DetectedGraphicsApi api) {
    switch (api) {
        case DetectedGraphicsApi::Direct3D12: return "Direct3D 12";
        case DetectedGraphicsApi::Direct3D11: return "Direct3D 11";
        case DetectedGraphicsApi::Direct3D10: return "Direct3D 10";
        case DetectedGraphicsApi::Direct3D9:  return "Direct3D 9";
        case DetectedGraphicsApi::Vulkan:     return "Vulkan";
        case DetectedGraphicsApi::OpenGL:     return "OpenGL";
        case DetectedGraphicsApi::NoneDetected: return "None / Software";
    }
    return "None / Software";
}

struct PeSectionInfo {
    std::string name;
    uint32_t virtual_address = 0;
    uint32_t virtual_size = 0;
    uint32_t raw_offset = 0;
    uint32_t raw_size = 0;
    uint32_t characteristics = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
    double shannon_entropy = 0.0;
};

struct PeImportModule {
    std::string dll_name;
    std::vector<std::string> functions;
    uint32_t ordinal_import_count = 0;
};

struct PeSecurityFlags {
    bool dynamic_base_aslr = false;
    bool high_entropy_va = false;
    bool nx_compat_dep = false;
    bool control_flow_guard = false;
    bool has_authenticode_sig = false;
    bool has_clr_dotnet_header = false;
    bool has_tls_callbacks = false;
    bool has_exception_pdata = false;
};

struct PeInspectionReport {
    bool valid_pe = false;
    std::string error_message;
    std::string source_path;
    uint64_t file_size_bytes = 0;

    PeArchitecture architecture = PeArchitecture::Unknown;
    PeFormat format = PeFormat::Unknown;
    PeSubsystem subsystem = PeSubsystem::Unknown;
    uint16_t raw_machine = 0;
    uint32_t timestamp = 0;
    uint32_t entry_point_rva = 0;
    uint64_t image_base = 0;
    uint32_t size_of_image = 0;
    bool is_dll = false;
    bool is_executable_image = false;
    bool large_address_aware = false;

    PeSecurityFlags security;
    std::vector<PeSectionInfo> sections;
    std::vector<PeImportModule> imports;

    std::vector<DetectedGraphicsApi> detected_graphics_apis;
    std::vector<std::string> detected_input_apis;
    std::vector<std::string> detected_audio_apis;
    std::vector<std::string> detected_middleware_or_drm;

    // Honest feasibility assessment — static inspection NEVER implies execution works.
    bool can_execute_locally = false;
    bool verified_on_xbox_hardware = false;
    std::vector<std::string> xbox_blockers;
    std::vector<std::string> notes;
};

class PeInspector {
public:
    static PeInspectionReport inspect_file(const std::filesystem::path& path);
    static PeInspectionReport inspect_bytes(const std::vector<uint8_t>& data,
                                            const std::string& label = "<memory>");
};

} // namespace xwine::runtime
