#include "xwine/steam/steam_library.hpp"
#include "xwine/steam/vdf_parser.hpp"
#include "xwine/runtime/pe_inspector.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace xwine::steam {

namespace {

uint64_t parse_u64(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    return (end && *end == '\0') ? static_cast<uint64_t>(v) : 0;
}

uint32_t accent_for_appid(uint32_t app_id) {
    static constexpr uint32_t kPalette[] = {
        0x0EA5E9, 0x22D3EE, 0x38BDF8, 0x6366F1, 0x0284C7, 0x14B8A6
    };
    return kPalette[app_id % 6];
}

} // namespace

std::optional<SteamAppManifest> SteamLibraryImporter::parse_app_manifest_file(
    const std::filesystem::path& acf_path, std::string* error_out) {
    std::ifstream in(acf_path);
    if (!in.is_open()) {
        if (error_out) *error_out = "Unable to open ACF file: " + acf_path.string();
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = VdfParser::parse(ss.str());
    if (!parsed.ok) {
        if (error_out) *error_out = "VDF parse error in " + acf_path.string() + ": " + parsed.error;
        return std::nullopt;
    }

    const VdfNode* app_state = parsed.root.find_child("AppState");
    if (!app_state || !app_state->is_section) {
        if (error_out) *error_out = "Missing AppState section in " + acf_path.string();
        return std::nullopt;
    }

    SteamAppManifest m;
    m.app_id = static_cast<uint32_t>(parse_u64(app_state->child_value("appid")));
    m.name = app_state->child_value("name");
    m.install_dir = app_state->child_value("installdir");
    m.size_on_disk_bytes = parse_u64(app_state->child_value("SizeOnDisk"));
    m.state_flags = static_cast<uint32_t>(parse_u64(app_state->child_value("StateFlags")));
    m.last_updated = parse_u64(app_state->child_value("LastUpdated"));
    m.build_id = parse_u64(app_state->child_value("buildid"));
    m.library_root = acf_path.parent_path().string();
    m.manifest_file_path = acf_path.string();

    if (m.app_id == 0 || m.name.empty()) {
        if (error_out) *error_out = "Incomplete AppState metadata (missing appid or name).";
        return std::nullopt;
    }
    return m;
}

std::vector<std::filesystem::path> SteamLibraryImporter::parse_library_folders_vdf(
    const std::filesystem::path& vdf_path) {
    std::vector<std::filesystem::path> roots;
    std::ifstream in(vdf_path);
    if (!in.is_open()) return roots;
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = VdfParser::parse(ss.str());
    if (!parsed.ok) return roots;

    const VdfNode* lf = parsed.root.find_child("libraryfolders");
    if (!lf || !lf->is_section) return roots;

    for (const auto& entry : lf->children) {
        if (entry.is_section) {
            std::string p = entry.child_value("path");
            if (!p.empty()) {
                roots.push_back(std::filesystem::path(p) / "steamapps");
            }
        } else if (!entry.value.empty()) {
            // Legacy VDF format ("1" "/path/to/library")
            if (std::all_of(entry.key.begin(), entry.key.end(),
                            [](unsigned char c) { return std::isdigit(c) != 0; })) {
                roots.push_back(std::filesystem::path(entry.value) / "steamapps");
            }
        }
    }
    return roots;
}

SteamScanReport SteamLibraryImporter::scan_steamapps_directory(const std::filesystem::path& steamapps_dir) {
    SteamScanReport report;
    std::error_code ec;
    if (!std::filesystem::exists(steamapps_dir, ec) || !std::filesystem::is_directory(steamapps_dir, ec)) {
        report.warnings.push_back("Steam directory not found: " + steamapps_dir.string());
        return report;
    }
    report.root_found = true;

    std::vector<std::filesystem::path> dirs_to_scan = {steamapps_dir};
    const auto vdf_path = steamapps_dir / "libraryfolders.vdf";
    if (std::filesystem::exists(vdf_path, ec)) {
        for (const auto& extra : parse_library_folders_vdf(vdf_path)) {
            if (std::filesystem::exists(extra, ec) &&
                std::find(dirs_to_scan.begin(), dirs_to_scan.end(), extra) == dirs_to_scan.end()) {
                dirs_to_scan.push_back(extra);
            }
        }
    }

    for (const auto& dir : dirs_to_scan) {
        report.scanned_libraries.push_back(dir);
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string fname = entry.path().filename().string();
            if (fname.rfind("appmanifest_", 0) == 0 && entry.path().extension() == ".acf") {
                std::string err;
                if (auto manifest = parse_app_manifest_file(entry.path(), &err)) {
                    report.manifests.push_back(std::move(*manifest));
                } else {
                    report.warnings.push_back(err);
                }
            }
        }
    }

    std::sort(report.manifests.begin(), report.manifests.end(),
              [](const SteamAppManifest& a, const SteamAppManifest& b) { return a.name < b.name; });
    return report;
}

runtime::GameEntry SteamLibraryImporter::to_game_entry(const SteamAppManifest& manifest, bool inspect_local_exe) {
    runtime::GameEntry g;
    g.id = "steam-" + std::to_string(manifest.app_id);
    g.title = manifest.name;
    g.developer = "Steam AppID " + std::to_string(manifest.app_id);
    g.publisher = "Imported from Steam AppManifest";
    g.genre = "Steam Library";
    g.source = "SteamMetadata";
    g.steam_app_id = manifest.app_id;
    g.accent_color_rgb = accent_for_appid(manifest.app_id);
    g.size_on_disk_bytes = manifest.size_on_disk_bytes;
    g.installed = (manifest.state_flags & 4U) != 0; // Steam StateFullyInstalled bit
    g.drm_free_declared = false;

    const std::filesystem::path game_dir =
        std::filesystem::path(manifest.library_root) / "common" / manifest.install_dir;
    g.install_directory = game_dir.string();

    std::optional<runtime::PeInspectionReport> pe_report;
    if (inspect_local_exe && std::filesystem::exists(game_dir)) {
        auto exes = runtime::LibraryManager::discover_executables(game_dir, 8);
        if (!exes.empty()) {
            std::error_code ec;
            auto rel = std::filesystem::relative(exes.front(), game_dir, ec);
            g.executable_relative_path = ec ? exes.front().filename().string() : rel.string();
            pe_report = runtime::PeInspector::inspect_file(exes.front());
            if (pe_report->valid_pe) {
                g.pe_inspected = true;
                g.pe_architecture = runtime::to_string(pe_report->architecture);
                g.pe_format = runtime::to_string(pe_report->format);
                g.pe_subsystem = runtime::to_string(pe_report->subsystem);
                g.pe_graphics_api = pe_report->detected_graphics_apis.empty()
                                        ? "None / Software"
                                        : runtime::to_string(pe_report->detected_graphics_apis.front());
                g.pe_import_count = static_cast<uint32_t>(pe_report->imports.size());
                for (const auto& imp : pe_report->imports) {
                    g.pe_imported_dlls.push_back(imp.dll_name);
                }
            }
        }
    }

    auto assessment = SteamCompatibilityEvaluator::evaluate(manifest, pe_report);
    g.compatibility = assessment.rating;
    g.compatibility_summary = assessment.summary;
    g.blockers = assessment.blockers;
    g.description = "Legitimately owned Steam library metadata imported from appmanifest_" +
                    std::to_string(manifest.app_id) + ".acf (Build " + std::to_string(manifest.build_id) +
                    "). Importing metadata does not imply Xbox execution compatibility.";
    return g;
}

} // namespace xwine::steam
