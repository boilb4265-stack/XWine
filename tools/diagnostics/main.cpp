#include "xwine/graphics/graphics_backend.hpp"
#include "xwine/input/controller_mapping.hpp"
#include "xwine/platform/benchmark.hpp"
#include "xwine/platform/platform.hpp"
#include "xwine/platform/xbox_capabilities.hpp"
#include "xwine/runtime/pe_inspector.hpp"
#include "xwine/steam/steam_library.hpp"
#include "xwine/steam/vdf_parser.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

void print_usage() {
    std::cout << "xwine-diag — Project XWine Diagnostic & PE Inspection CLI (Experimental)\n"
              << "Usage:\n"
              << "  xwine-diag sysinfo                 Show host & Xbox Series S target profile\n"
              << "  xwine-diag inspect <file.exe>      Static non-executing PE32/PE32+ analysis\n"
              << "  xwine-diag steam <steamapps_dir>   Scan & parse Steam VDF/ACF manifests\n"
              << "  xwine-diag bench                   Run local subsystem micro-benchmarks\n";
}

int cmd_sysinfo() {
    auto plat = xwine::platform::create_host_platform();
    const auto host = plat->host_hardware();
    const auto xbox_game = plat->target_xbox_series_s_profile(true);
    const auto xbox_app = plat->target_xbox_series_s_profile(false);
    const auto metrics = plat->query_metrics(xbox_game.app_usable_ram_bytes);

    std::cout << "=== Project XWine System & Target Report ===\n"
              << "Host Platform      : " << xwine::platform::to_string(plat->platform_id()) << "\n"
              << "Host CPU           : " << host.cpu_model << " (" << host.cpu_architecture << ", "
              << host.logical_cores << " logical cores)\n"
              << "Host Physical RAM  : " << (host.physical_ram_bytes / (1024ULL * 1024ULL)) << " MiB\n"
              << "Process RSS        : " << (metrics.process_rss_bytes / (1024ULL * 1024ULL)) << " MiB\n\n"
              << "Target Hardware    : " << xbox_game.device_name << "\n"
              << "Target CPU / GPU   : " << xbox_game.cpu_model << " | " << xbox_game.gpu_name << "\n"
              << "Physical RAM       : " << (xbox_game.physical_ram_bytes / (1024ULL * 1024ULL)) << " MiB\n"
              << "Dev Mode Game Cap  : " << (xbox_game.app_usable_ram_bytes / (1024ULL * 1024ULL)) << " MiB\n"
              << "Dev Mode App Cap   : " << (xbox_app.app_usable_ram_bytes / (1024ULL * 1024ULL)) << " MiB\n"
              << "Hardware Verified  : " << (xbox_game.hardware_verified ? "YES" : "NO (Phase 6 Pending)") << "\n\n"
              << "=== Graphics Translation Matrix (Xbox Series S UWP) ===\n";

    xwine::graphics::GraphicsCapabilityRegistry gfx;
    for (const auto& r : gfx.all_xbox_routes()) {
        std::cout << "  - " << std::setw(12) << std::left << xwine::graphics::to_string(r.guest_api)
                  << " | " << std::setw(24) << xwine::platform::to_string(r.status)
                  << " | " << r.route_name << "\n";
    }
    return 0;
}

int cmd_inspect(const std::string& file_path) {
    auto rep = xwine::runtime::PeInspector::inspect_file(file_path);
    if (!rep.valid_pe) {
        std::cerr << "PE inspection failed: " << rep.error_message << "\n";
        return 2;
    }

    std::cout << "=== XWine Static PE Inspection Report (Non-Executing) ===\n"
              << "File              : " << rep.source_path << " (" << rep.file_size_bytes << " bytes)\n"
              << "Format / Arch     : " << xwine::runtime::to_string(rep.format) << " / "
              << xwine::runtime::to_string(rep.architecture) << "\n"
              << "Subsystem         : " << xwine::runtime::to_string(rep.subsystem) << "\n"
              << "Entry Point RVA   : 0x" << std::hex << rep.entry_point_rva
              << " | ImageBase: 0x" << rep.image_base << std::dec << "\n"
              << "Security Flags    : ASLR=" << rep.security.dynamic_base_aslr
              << " DEP=" << rep.security.nx_compat_dep
              << " CFG=" << rep.security.control_flow_guard
              << " CLR=" << rep.security.has_clr_dotnet_header << "\n"
              << "Sections (" << rep.sections.size() << "):\n";

    for (const auto& s : rep.sections) {
        std::cout << "  - " << std::setw(10) << std::left << s.name
                  << " VA=0x" << std::hex << std::setw(8) << s.virtual_address << std::dec
                  << " Raw=" << s.raw_size
                  << " Perms=" << (s.readable ? "R" : "-") << (s.writable ? "W" : "-") << (s.executable ? "X" : "-")
                  << " Entropy=" << std::fixed << std::setprecision(2) << s.shannon_entropy << "\n";
    }

    std::cout << "Imported DLLs (" << rep.imports.size() << "):\n";
    for (const auto& imp : rep.imports) {
        std::cout << "  - " << imp.dll_name << " (" << imp.functions.size() << " named, "
                  << imp.ordinal_import_count << " ordinal)\n";
    }

    std::cout << "Can Execute Here  : " << (rep.can_execute_locally ? "YES" : "NO (Inspection Only)") << "\n"
              << "Xbox Verified     : " << (rep.verified_on_xbox_hardware ? "YES" : "NO") << "\n"
              << "Xbox Blockers (" << rep.xbox_blockers.size() << "):\n";
    for (const auto& b : rep.xbox_blockers) {
        std::cout << "  [!] " << b << "\n";
    }
    return 0;
}

int cmd_steam(const std::string& dir) {
    auto scan = xwine::steam::SteamLibraryImporter::scan_steamapps_directory(dir);
    std::cout << "=== XWine Steam Metadata Scan ===\n"
              << "Root Found        : " << (scan.root_found ? "YES" : "NO") << "\n"
              << "Manifests Parsed  : " << scan.manifests.size() << "\n";
    for (const auto& m : scan.manifests) {
        std::cout << "  - [" << m.app_id << "] " << m.name << " ("
                  << (m.size_on_disk_bytes / (1024ULL * 1024ULL)) << " MiB) [Metadata Only]\n";
    }
    for (const auto& w : scan.warnings) {
        std::cout << "  [WARN] " << w << "\n";
    }
    return scan.root_found ? 0 : 1;
}

int cmd_bench() {
    using clock = std::chrono::steady_clock;

    // 1. VDF Parser Benchmark
    const std::string sample_vdf =
        "\"AppState\"\n{\n\"appid\" \"413150\"\n\"name\" \"Stardew Valley\"\n"
        "\"installdir\" \"Stardew Valley\"\n\"SizeOnDisk\" \"681574400\"\n}\n";
    auto t0 = clock::now();
    constexpr int kVdfIters = 10000;
    int ok_count = 0;
    for (int i = 0; i < kVdfIters; ++i) {
        if (xwine::steam::VdfParser::parse(sample_vdf).ok) ++ok_count;
    }
    double vdf_ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();

    // 2. Controller Deadzone & Curve Math Benchmark
    auto t1 = clock::now();
    constexpr int kInputIters = 100000;
    double accum = 0.0;
    for (int i = 0; i < kInputIters; ++i) {
        float x = static_cast<float>((i % 200) - 100) / 100.0f;
        float y = static_cast<float>(((i * 7) % 200) - 100) / 100.0f;
        auto v = xwine::input::apply_radial_deadzone_and_curve(x, y, 0.15f, 1.2f, false);
        accum += v.x + v.y;
    }
    double input_ms = std::chrono::duration<double, std::milli>(clock::now() - t1).count();

    std::cout << "=== XWine Subsystem Micro-Benchmarks (Real Measured Timings) ===\n"
              << "VDF Parse (" << kVdfIters << " iters, " << ok_count << " ok) : "
              << std::fixed << std::setprecision(2) << vdf_ms << " ms\n"
              << "Input Deadzone/Curve (" << kInputIters << " iters)       : "
              << input_ms << " ms (checksum=" << std::setprecision(3) << accum << ")\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }
    const std::string cmd = argv[1];
    if (cmd == "sysinfo") return cmd_sysinfo();
    if (cmd == "inspect" && argc >= 3) return cmd_inspect(argv[2]);
    if (cmd == "steam" && argc >= 3) return cmd_steam(argv[2]);
    if (cmd == "bench") return cmd_bench();

    print_usage();
    return 1;
}
