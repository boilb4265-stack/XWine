#include "xwine/runtime/pe_inspector.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

namespace xwine::runtime {

namespace {

constexpr uint16_t kDosMagic = 0x5A4D; // "MZ"
constexpr uint32_t kPeSignature = 0x00004550; // "PE\0\0"

constexpr uint16_t kMachineI386  = 0x014c;
constexpr uint16_t kMachineAmd64 = 0x8664;
constexpr uint16_t kMachineArm64 = 0xAA64;
constexpr uint16_t kMachineArmNt = 0x01c4;

constexpr uint16_t kOptMagicPE32     = 0x010b;
constexpr uint16_t kOptMagicPE32Plus = 0x020b;

// Data directory indices
constexpr size_t kDirImport    = 1;
constexpr size_t kDirException = 3;
constexpr size_t kDirSecurity  = 4;
constexpr size_t kDirTls       = 9;
constexpr size_t kDirComClr    = 14;

template <typename T>
bool read_le(const std::vector<uint8_t>& buf, size_t offset, T& out) {
    if (offset > buf.size() || sizeof(T) > buf.size() - offset) {
        return false;
    }
    std::memcpy(&out, buf.data() + offset, sizeof(T));
    return true;
}

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool read_cstring(const std::vector<uint8_t>& buf, size_t offset, std::string& out, size_t max_len = 256) {
    if (offset >= buf.size()) return false;
    out.clear();
    const size_t limit = std::min(buf.size(), offset + max_len);
    for (size_t i = offset; i < limit; ++i) {
        if (buf[i] == 0) {
            return true;
        }
        out.push_back(static_cast<char>(buf[i]));
    }
    return false;
}

double compute_shannon_entropy(const std::vector<uint8_t>& buf, size_t offset, size_t size) {
    if (size == 0 || offset >= buf.size()) return 0.0;
    const size_t clamped = std::min(size, buf.size() - offset);
    if (clamped == 0) return 0.0;

    std::array<uint64_t, 256> counts{};
    for (size_t i = 0; i < clamped; ++i) {
        ++counts[buf[offset + i]];
    }
    double entropy = 0.0;
    const double total = static_cast<double>(clamped);
    for (uint64_t c : counts) {
        if (c == 0) continue;
        const double p = static_cast<double>(c) / total;
        entropy -= p * std::log2(p);
    }
    return entropy;
}

bool rva_to_offset(uint32_t rva, const std::vector<PeSectionInfo>& sections, size_t& file_offset_out) {
    for (const auto& sec : sections) {
        const uint32_t span = std::max(sec.virtual_size, sec.raw_size);
        if (span == 0) continue;
        if (rva >= sec.virtual_address && rva < sec.virtual_address + span) {
            const uint32_t delta = rva - sec.virtual_address;
            if (delta < sec.raw_size) {
                file_offset_out = static_cast<size_t>(sec.raw_offset) + static_cast<size_t>(delta);
                return true;
            }
        }
    }
    return false;
}

void push_unique_api(std::vector<DetectedGraphicsApi>& list, DetectedGraphicsApi api) {
    if (std::find(list.begin(), list.end(), api) == list.end()) {
        list.push_back(api);
    }
}

void push_unique_str(std::vector<std::string>& list, const std::string& val) {
    if (std::find(list.begin(), list.end(), val) == list.end()) {
        list.push_back(val);
    }
}

void classify_dependencies_and_blockers(PeInspectionReport& report) {
    report.can_execute_locally = false;
    report.verified_on_xbox_hardware = false;

    report.notes.push_back(
        "Static PE metadata inspection completed safely without executing binary instructions.");

    if (report.architecture == PeArchitecture::X86_32) {
        report.xbox_blockers.push_back(
            "32-bit x86 (PE32): Xbox UWP runs 64-bit x64 processes without desktop WoW64; requires software x86->x64 JIT.");
    } else if (report.architecture == PeArchitecture::ARM64 || report.architecture == PeArchitecture::ARM32) {
        report.xbox_blockers.push_back(
            "ARM binary incompatible with Xbox Series S AMD Zen 2 x86-64 CPU without ISA emulation.");
    } else if (report.architecture == PeArchitecture::Unknown) {
        report.xbox_blockers.push_back("Unrecognized CPU machine architecture in COFF header.");
    }

    if (report.security.has_clr_dotnet_header) {
        push_unique_str(report.detected_middleware_or_drm, ".NET / CLR Managed Assembly");
        report.xbox_blockers.push_back(
            "Managed .NET/CLR binary requires a .NET runtime host rather than direct native PE execution.");
    }

    bool has_user32 = false;
    bool has_gdi32 = false;
    bool has_advapi32 = false;
    bool has_shell32 = false;
    bool has_vulkan = false;
    bool has_opengl = false;
    bool has_d3d9 = false;
    bool has_d3d11 = false;
    bool has_d3d12 = false;

    for (const auto& imp : report.imports) {
        const std::string dll = to_lower_ascii(imp.dll_name);

        if (dll == "d3d12.dll") {
            has_d3d12 = true;
            push_unique_api(report.detected_graphics_apis, DetectedGraphicsApi::Direct3D12);
        } else if (dll == "d3d11.dll") {
            has_d3d11 = true;
            push_unique_api(report.detected_graphics_apis, DetectedGraphicsApi::Direct3D11);
        } else if (dll == "d3d10.dll" || dll == "d3d10_1.dll") {
            push_unique_api(report.detected_graphics_apis, DetectedGraphicsApi::Direct3D10);
        } else if (dll == "d3d9.dll") {
            has_d3d9 = true;
            push_unique_api(report.detected_graphics_apis, DetectedGraphicsApi::Direct3D9);
        } else if (dll == "vulkan-1.dll") {
            has_vulkan = true;
            push_unique_api(report.detected_graphics_apis, DetectedGraphicsApi::Vulkan);
        } else if (dll == "opengl32.dll") {
            has_opengl = true;
            push_unique_api(report.detected_graphics_apis, DetectedGraphicsApi::OpenGL);
        }

        if (dll.rfind("xinput", 0) == 0) push_unique_str(report.detected_input_apis, "XInput (" + imp.dll_name + ")");
        if (dll.rfind("dinput", 0) == 0) push_unique_str(report.detected_input_apis, "DirectInput (" + imp.dll_name + ")");
        if (dll.rfind("xaudio2", 0) == 0) push_unique_str(report.detected_audio_apis, "XAudio2 (" + imp.dll_name + ")");
        if (dll == "dsound.dll") push_unique_str(report.detected_audio_apis, "DirectSound");
        if (dll == "winmm.dll") push_unique_str(report.detected_audio_apis, "WinMM");
        if (dll.rfind("fmod", 0) == 0) push_unique_str(report.detected_audio_apis, "FMOD");

        if (dll == "user32.dll") has_user32 = true;
        if (dll == "gdi32.dll") has_gdi32 = true;
        if (dll == "advapi32.dll") has_advapi32 = true;
        if (dll == "shell32.dll") has_shell32 = true;

        if (dll.find("steam_api") != std::string::npos) {
            push_unique_str(report.detected_middleware_or_drm, "Steamworks API (" + imp.dll_name + ")");
            report.xbox_blockers.push_back(
                "Imports " + imp.dll_name + ": Requires Steam client IPC / DRM checks (not available in Xbox UWP; never bypassed).");
        }
        if (dll.find("easyanticheat") != std::string::npos || dll.find("eac_") != std::string::npos) {
            push_unique_str(report.detected_middleware_or_drm, "Easy Anti-Cheat");
            report.xbox_blockers.push_back(
                "Imports Easy Anti-Cheat: Kernel anti-cheat drivers are strictly impossible in Xbox Dev Mode.");
        }
        if (dll.find("beclient") != std::string::npos || dll.find("battleye") != std::string::npos) {
            push_unique_str(report.detected_middleware_or_drm, "BattlEye Anti-Cheat");
            report.xbox_blockers.push_back(
                "Imports BattlEye: Kernel anti-cheat drivers are strictly impossible in Xbox Dev Mode.");
        }
        if (dll.find("eossdk") != std::string::npos) {
            push_unique_str(report.detected_middleware_or_drm, "Epic Online Services SDK");
        }
    }

    if (has_vulkan) {
        report.xbox_blockers.push_back(
            "Imports vulkan-1.dll: Xbox OS provides no Vulkan ICD (requires experimental Mesa Dozen over D3D12).");
    }
    if (has_opengl) {
        report.xbox_blockers.push_back(
            "Imports opengl32.dll: Xbox OS provides no desktop OpenGL driver (requires Mesa Gallium D3D12 / ANGLE).");
    }
    if (has_d3d9) {
        report.xbox_blockers.push_back(
            "Imports d3d9.dll: Xbox UWP lacks native D3D9; requires D3D9On12 mapping layer.");
    }
    if (has_d3d12 || has_d3d11) {
        report.notes.push_back(
            "Direct3D 11/12 detected: Favorable for future Xbox UWP Game Mode native pass-through with CoreWindow swapchain shimming.");
    }
    if (has_user32) {
        report.xbox_blockers.push_back(
            "Imports user32.dll: Desktop HWND/message loop unavailable in OneCoreUAP; requires XWine virtual window shim.");
    }
    if (has_gdi32) {
        report.xbox_blockers.push_back(
            "Imports gdi32.dll: Desktop GDI unavailable in Xbox UWP; requires software/D3D GDI shim.");
    }
    if (has_advapi32) {
        report.notes.push_back(
            "Imports advapi32.dll: Registry/service APIs require virtualized in-process registry in AppContainer.");
    }
    if (has_shell32) {
        report.notes.push_back(
            "Imports shell32.dll: Desktop shell/folder paths must redirect into Xbox AppContainer LocalFolder.");
    }

    for (const auto& sec : report.sections) {
        if (sec.executable && sec.writable) {
            report.xbox_blockers.push_back(
                "Section '" + sec.name + "' requests RWX permissions: Violates strict UWP W^X policy; needs controlled VirtualProtectFromApp transitions.");
        }
        if (sec.shannon_entropy > 7.45 && sec.raw_size > 4096) {
            push_unique_str(report.detected_middleware_or_drm,
                            "High-entropy section '" + sec.name + "' (possible packer/DRM)");
        }
    }

    if (report.detected_graphics_apis.empty()) {
        report.detected_graphics_apis.push_back(DetectedGraphicsApi::NoneDetected);
    }

    report.xbox_blockers.push_back(
        "Phase 7 in-process PE execution layer is not yet implemented or verified on physical Xbox Series S hardware.");
}

} // namespace

PeInspectionReport PeInspector::inspect_file(const std::filesystem::path& path) {
    PeInspectionReport report;
    report.source_path = path.string();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        report.error_message = "File does not exist or is not a regular file: " + path.string();
        return report;
    }

    const uint64_t fsize = std::filesystem::file_size(path, ec);
    if (ec) {
        report.error_message = "Failed to query file size: " + ec.message();
        return report;
    }
    constexpr uint64_t kMaxInspectBytes = 256ULL * 1024ULL * 1024ULL; // 256 MiB safety cap
    if (fsize > kMaxInspectBytes) {
        report.error_message = "File exceeds 256 MiB safe static inspection cap.";
        return report;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        report.error_message = "Unable to open PE file for reading: " + path.string();
        return report;
    }

    std::vector<uint8_t> data(static_cast<size_t>(fsize));
    if (fsize > 0 && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fsize))) {
        report.error_message = "Failed to read complete PE file contents.";
        return report;
    }

    return inspect_bytes(data, path.string());
}

PeInspectionReport PeInspector::inspect_bytes(const std::vector<uint8_t>& data, const std::string& label) {
    PeInspectionReport report;
    report.source_path = label;
    report.file_size_bytes = data.size();

    if (data.size() < 64) {
        report.error_message = "Buffer too small to contain MS-DOS header (< 64 bytes).";
        return report;
    }

    uint16_t dos_magic = 0;
    uint32_t pe_offset = 0;
    if (!read_le(data, 0, dos_magic) || dos_magic != kDosMagic) {
        report.error_message = "Invalid MS-DOS signature (expected 'MZ').";
        return report;
    }
    if (!read_le(data, 0x3C, pe_offset)) {
        report.error_message = "Failed to read e_lfanew offset.";
        return report;
    }

    uint32_t pe_sig = 0;
    if (!read_le(data, pe_offset, pe_sig) || pe_sig != kPeSignature) {
        report.error_message = "Invalid PE signature (expected 'PE\\0\\0' at e_lfanew).";
        return report;
    }

    // COFF File Header (20 bytes immediately following 4-byte PE signature)
    const size_t coff_off = static_cast<size_t>(pe_offset) + 4;
    uint16_t machine = 0;
    uint16_t num_sections = 0;
    uint32_t timestamp = 0;
    uint16_t opt_header_size = 0;
    uint16_t characteristics = 0;

    if (!read_le(data, coff_off + 0, machine) ||
        !read_le(data, coff_off + 2, num_sections) ||
        !read_le(data, coff_off + 4, timestamp) ||
        !read_le(data, coff_off + 16, opt_header_size) ||
        !read_le(data, coff_off + 18, characteristics)) {
        report.error_message = "Truncated COFF file header.";
        return report;
    }

    report.raw_machine = machine;
    report.timestamp = timestamp;
    report.is_executable_image = (characteristics & 0x0002) != 0;
    report.large_address_aware = (characteristics & 0x0020) != 0;
    report.is_dll = (characteristics & 0x2000) != 0;

    switch (machine) {
        case kMachineI386:  report.architecture = PeArchitecture::X86_32; break;
        case kMachineAmd64: report.architecture = PeArchitecture::X86_64; break;
        case kMachineArm64: report.architecture = PeArchitecture::ARM64;  break;
        case kMachineArmNt: report.architecture = PeArchitecture::ARM32;  break;
        default:            report.architecture = PeArchitecture::Unknown; break;
    }

    const size_t opt_off = coff_off + 20;
    if (opt_header_size < 2 || opt_off + opt_header_size > data.size()) {
        report.error_message = "Truncated or missing PE Optional Header.";
        return report;
    }

    uint16_t opt_magic = 0;
    read_le(data, opt_off, opt_magic);
    bool is_pe32_plus = false;
    if (opt_magic == kOptMagicPE32) {
        report.format = PeFormat::PE32;
        is_pe32_plus = false;
    } else if (opt_magic == kOptMagicPE32Plus) {
        report.format = PeFormat::PE32Plus;
        is_pe32_plus = true;
    } else {
        report.error_message = "Unknown PE Optional Header magic.";
        return report;
    }

    read_le(data, opt_off + 16, report.entry_point_rva);
    if (is_pe32_plus) {
        read_le(data, opt_off + 24, report.image_base);
    } else {
        uint32_t base32 = 0;
        read_le(data, opt_off + 28, base32);
        report.image_base = base32;
    }
    read_le(data, opt_off + 56, report.size_of_image);

    uint16_t raw_subsystem = 0;
    uint16_t dll_chars = 0;
    read_le(data, opt_off + 68, raw_subsystem);
    read_le(data, opt_off + 70, dll_chars);

    switch (raw_subsystem) {
        case 1:  report.subsystem = PeSubsystem::Native; break;
        case 2:  report.subsystem = PeSubsystem::WindowsGui; break;
        case 3:  report.subsystem = PeSubsystem::WindowsConsole; break;
        case 10: report.subsystem = PeSubsystem::EfiApplication; break;
        case 14: report.subsystem = PeSubsystem::Xbox; break;
        default: report.subsystem = PeSubsystem::Unknown; break;
    }

    report.security.high_entropy_va    = (dll_chars & 0x0020) != 0;
    report.security.dynamic_base_aslr  = (dll_chars & 0x0040) != 0;
    report.security.nx_compat_dep      = (dll_chars & 0x0100) != 0;
    report.security.control_flow_guard = (dll_chars & 0x4000) != 0;

    // Data Directories
    const size_t num_rva_off = opt_off + (is_pe32_plus ? 108 : 92);
    const size_t dir_table_off = opt_off + (is_pe32_plus ? 112 : 96);
    uint32_t num_dirs = 0;
    if (num_rva_off + 4 <= opt_off + opt_header_size) {
        read_le(data, num_rva_off, num_dirs);
        num_dirs = std::min<uint32_t>(num_dirs, 16);
    }

    auto read_dir = [&](size_t idx, uint32_t& rva, uint32_t& size) -> bool {
        if (idx >= num_dirs) return false;
        const size_t off = dir_table_off + idx * 8;
        if (off + 8 > opt_off + opt_header_size) return false;
        return read_le(data, off, rva) && read_le(data, off + 4, size);
    };

    uint32_t import_rva = 0, import_size = 0;
    read_dir(kDirImport, import_rva, import_size);

    uint32_t exc_rva = 0, exc_size = 0;
    if (read_dir(kDirException, exc_rva, exc_size) && exc_rva != 0 && exc_size != 0) {
        report.security.has_exception_pdata = true;
    }
    uint32_t sec_rva = 0, sec_size = 0;
    if (read_dir(kDirSecurity, sec_rva, sec_size) && sec_rva != 0 && sec_size != 0) {
        report.security.has_authenticode_sig = true;
    }
    uint32_t tls_rva = 0, tls_size = 0;
    if (read_dir(kDirTls, tls_rva, tls_size) && tls_rva != 0 && tls_size != 0) {
        report.security.has_tls_callbacks = true;
    }
    uint32_t clr_rva = 0, clr_size = 0;
    if (read_dir(kDirComClr, clr_rva, clr_size) && clr_rva != 0 && clr_size != 0) {
        report.security.has_clr_dotnet_header = true;
    }

    // Section Table (40 bytes per section)
    const size_t sec_table_off = opt_off + opt_header_size;
    if (num_sections > 128) {
        report.error_message = "Abnormally large PE section count (> 128).";
        return report;
    }
    for (uint16_t i = 0; i < num_sections; ++i) {
        const size_t off = sec_table_off + static_cast<size_t>(i) * 40;
        if (off + 40 > data.size()) {
            report.error_message = "Truncated PE section header table.";
            return report;
        }
        PeSectionInfo sec;
        char raw_name[9]{};
        std::memcpy(raw_name, data.data() + off, 8);
        sec.name = std::string(raw_name);
        read_le(data, off + 8, sec.virtual_size);
        read_le(data, off + 12, sec.virtual_address);
        read_le(data, off + 16, sec.raw_size);
        read_le(data, off + 20, sec.raw_offset);
        read_le(data, off + 36, sec.characteristics);
        sec.executable = (sec.characteristics & 0x20000000U) != 0;
        sec.readable   = (sec.characteristics & 0x40000000U) != 0;
        sec.writable   = (sec.characteristics & 0x80000000U) != 0;
        sec.shannon_entropy = compute_shannon_entropy(data, sec.raw_offset, sec.raw_size);
        report.sections.push_back(std::move(sec));
    }

    // Parse Import Directory Table (IMAGE_IMPORT_DESCRIPTOR = 20 bytes each)
    size_t import_off = 0;
    if (import_rva != 0 && rva_to_offset(import_rva, report.sections, import_off)) {
        constexpr size_t kMaxImportModules = 512;
        for (size_t idx = 0; idx < kMaxImportModules; ++idx) {
            const size_t desc_off = import_off + idx * 20;
            uint32_t orig_first_thunk = 0;
            uint32_t name_rva = 0;
            uint32_t first_thunk = 0;
            if (!read_le(data, desc_off + 0, orig_first_thunk) ||
                !read_le(data, desc_off + 12, name_rva) ||
                !read_le(data, desc_off + 16, first_thunk)) {
                break;
            }
            if (orig_first_thunk == 0 && name_rva == 0 && first_thunk == 0) {
                break; // Null terminator descriptor
            }

            size_t name_off = 0;
            std::string dll_name;
            if (!rva_to_offset(name_rva, report.sections, name_off) ||
                !read_cstring(data, name_off, dll_name)) {
                continue;
            }

            PeImportModule mod;
            mod.dll_name = dll_name;

            const uint32_t thunk_rva = (orig_first_thunk != 0) ? orig_first_thunk : first_thunk;
            size_t thunk_off = 0;
            if (thunk_rva != 0 && rva_to_offset(thunk_rva, report.sections, thunk_off)) {
                constexpr size_t kMaxFunctionsPerDll = 2048;
                const size_t entry_size = is_pe32_plus ? 8 : 4;
                for (size_t f = 0; f < kMaxFunctionsPerDll; ++f) {
                    const size_t entry_off = thunk_off + f * entry_size;
                    if (is_pe32_plus) {
                        uint64_t val = 0;
                        if (!read_le(data, entry_off, val) || val == 0) break;
                        if ((val & (1ULL << 63)) != 0) {
                            ++mod.ordinal_import_count;
                        } else {
                            size_t hint_off = 0;
                            std::string fn_name;
                            if (rva_to_offset(static_cast<uint32_t>(val & 0x7FFFFFFFULL), report.sections, hint_off) &&
                                read_cstring(data, hint_off + 2, fn_name)) {
                                mod.functions.push_back(std::move(fn_name));
                            }
                        }
                    } else {
                        uint32_t val = 0;
                        if (!read_le(data, entry_off, val) || val == 0) break;
                        if ((val & 0x80000000U) != 0) {
                            ++mod.ordinal_import_count;
                        } else {
                            size_t hint_off = 0;
                            std::string fn_name;
                            if (rva_to_offset(val & 0x7FFFFFFFU, report.sections, hint_off) &&
                                read_cstring(data, hint_off + 2, fn_name)) {
                                mod.functions.push_back(std::move(fn_name));
                            }
                        }
                    }
                }
            }

            report.imports.push_back(std::move(mod));
        }
    }

    report.valid_pe = true;
    classify_dependencies_and_blockers(report);
    return report;
}

} // namespace xwine::runtime
