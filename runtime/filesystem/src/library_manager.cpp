#include "xwine/runtime/library_manager.hpp"
#include "xwine/platform/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace xwine::runtime {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

uint64_t now_epoch_sec() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string sanitize_id(const std::string& id) {
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

CompatibilityRating parse_compatibility_rating(const std::string& text) {
    if (text == "Inspect Only") return CompatibilityRating::InspectOnly;
    if (text == "Blocked (32-bit)") return CompatibilityRating::Blocked_32Bit;
    if (text == "Blocked (DRM)") return CompatibilityRating::Blocked_DRM;
    if (text == "Blocked (Anti-Cheat)") return CompatibilityRating::Blocked_AntiCheat;
    if (text == "Blocked (No Runtime)") return CompatibilityRating::Blocked_MissingRuntime;
    if (text == "Incompatible") return CompatibilityRating::Incompatible;
    if (text == "Experimental (Host)") return CompatibilityRating::Experimental_HostOnly;
    // Never deserialize Verified_Xbox blindly from unverified user files unless explicitly set in code tests
    return CompatibilityRating::Untested;
}

void LibraryManager::set_sandbox_root(const std::filesystem::path& root) {
    sandbox_root_ = root.lexically_normal();
}

bool LibraryManager::is_path_within_root(const std::filesystem::path& root,
                                         const std::filesystem::path& candidate) {
    if (root.empty() || candidate.empty()) return false;
    const auto norm_root = std::filesystem::weakly_canonical(root);
    const auto norm_cand = std::filesystem::weakly_canonical(candidate);

    auto root_it = norm_root.begin();
    auto cand_it = norm_cand.begin();
    for (; root_it != norm_root.end(); ++root_it, ++cand_it) {
        if (cand_it == norm_cand.end() || *root_it != *cand_it) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> LibraryManager::compute_file_fnv1a_hex(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    uint64_t hash = 14695981039346656037ULL;
    char buf[8192];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        const auto n = static_cast<size_t>(in.gcount());
        for (size_t i = 0; i < n; ++i) {
            hash ^= static_cast<uint8_t>(buf[i]);
            hash *= 1099511628211ULL;
        }
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(hex);
}

bool LibraryManager::add_or_update_game(const GameEntry& entry) {
    if (entry.id.empty() || entry.title.empty()) return false;

    // Honesty guard: never accept Verified_Xbox on unverified platforms.
    GameEntry clean = entry;
    if (clean.compatibility == CompatibilityRating::Verified_Xbox) {
        clean.compatibility = CompatibilityRating::Untested;
    }
    if (clean.added_epoch_sec == 0) {
        clean.added_epoch_sec = now_epoch_sec();
    }

    for (auto& existing : games_) {
        if (existing.id == clean.id) {
            existing = std::move(clean);
            return true;
        }
    }
    games_.push_back(std::move(clean));
    return true;
}

bool LibraryManager::remove_game(const std::string& id) {
    auto it = std::remove_if(games_.begin(), games_.end(),
                             [&id](const GameEntry& g) { return g.id == id; });
    if (it == games_.end()) return false;
    games_.erase(it, games_.end());
    return true;
}

GameEntry* LibraryManager::find_game(const std::string& id) {
    for (auto& g : games_) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

const GameEntry* LibraryManager::find_game(const std::string& id) const {
    for (const auto& g : games_) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

std::vector<const GameEntry*> LibraryManager::query(LibraryFilter filter, LibrarySort sort,
                                                    const std::string& search_query) const {
    const std::string needle = to_lower(search_query);
    std::vector<const GameEntry*> results;
    results.reserve(games_.size());

    for (const auto& g : games_) {
        bool pass = true;
        switch (filter) {
            case LibraryFilter::All: break;
            case LibraryFilter::Installed: pass = g.installed; break;
            case LibraryFilter::Favorites: pass = g.favorite; break;
            case LibraryFilter::RecentlyPlayed: pass = g.last_played_epoch_sec > 0; break;
            case LibraryFilter::Inspected: pass = g.pe_inspected; break;
            case LibraryFilter::DrmFree: pass = g.drm_free_declared; break;
        }
        if (!pass) continue;

        if (!needle.empty()) {
            const std::string hay = to_lower(g.title + " " + g.developer + " " + g.genre + " " + g.pe_graphics_api);
            if (hay.find(needle) == std::string::npos) continue;
        }
        results.push_back(&g);
    }

    switch (sort) {
        case LibrarySort::TitleAsc:
            std::sort(results.begin(), results.end(), [](const GameEntry* a, const GameEntry* b) {
                return to_lower(a->title) < to_lower(b->title);
            });
            break;
        case LibrarySort::RecentlyAddedDesc:
            std::sort(results.begin(), results.end(), [](const GameEntry* a, const GameEntry* b) {
                return a->added_epoch_sec > b->added_epoch_sec;
            });
            break;
        case LibrarySort::RecentlyPlayedDesc:
            std::sort(results.begin(), results.end(), [](const GameEntry* a, const GameEntry* b) {
                return a->last_played_epoch_sec > b->last_played_epoch_sec;
            });
            break;
        case LibrarySort::SizeDesc:
            std::sort(results.begin(), results.end(), [](const GameEntry* a, const GameEntry* b) {
                return a->size_on_disk_bytes > b->size_on_disk_bytes;
            });
            break;
    }

    return results;
}

bool LibraryManager::toggle_favorite(const std::string& id) {
    auto* g = find_game(id);
    if (!g) return false;
    g->favorite = !g->favorite;
    return true;
}

void LibraryManager::record_inspection_event(const std::string& id) {
    if (auto* g = find_game(id)) {
        g->last_played_epoch_sec = now_epoch_sec();
        ++g->play_count;
    }
}

PeInspectionReport LibraryManager::inspect_game_executable(const std::string& id) {
    PeInspectionReport report;
    auto* g = find_game(id);
    if (!g) {
        report.error_message = "Game ID not found in library: " + id;
        return report;
    }

    std::filesystem::path exe_path = std::filesystem::path(g->install_directory) / g->executable_relative_path;
    report = PeInspector::inspect_file(exe_path);

    g->pe_inspected = report.valid_pe;
    if (!report.valid_pe) {
        g->compatibility = CompatibilityRating::Blocked_MissingRuntime;
        g->compatibility_summary = "Executable missing or invalid PE image (" + report.error_message + ").";
        g->blockers = {report.error_message};
        return report;
    }

    g->pe_architecture = to_string(report.architecture);
    g->pe_format = to_string(report.format);
    g->pe_subsystem = to_string(report.subsystem);
    g->pe_graphics_api = report.detected_graphics_apis.empty()
                             ? "None / Software"
                             : to_string(report.detected_graphics_apis.front());
    g->pe_import_count = static_cast<uint32_t>(report.imports.size());
    g->pe_imported_dlls.clear();
    for (const auto& imp : report.imports) {
        g->pe_imported_dlls.push_back(imp.dll_name);
    }
    if (auto hash = compute_file_fnv1a_hex(exe_path)) {
        g->executable_checksum_fnv1a = *hash;
    }

    g->blockers = report.xbox_blockers;

    // Classify honestly — never claim Verified_Xbox from static inspection.
    bool has_anticheat = false;
    bool has_drm = false;
    for (const auto& mw : report.detected_middleware_or_drm) {
        const std::string low = to_lower(mw);
        if (low.find("anti-cheat") != std::string::npos || low.find("battleye") != std::string::npos) {
            has_anticheat = true;
        }
        if (low.find("steamworks") != std::string::npos || low.find("drm") != std::string::npos) {
            has_drm = true;
        }
    }

    if (has_anticheat) {
        g->compatibility = CompatibilityRating::Blocked_AntiCheat;
        g->compatibility_summary = "Blocked: Kernel anti-cheat cannot run in Xbox Developer Mode.";
    } else if (has_drm) {
        g->compatibility = CompatibilityRating::Blocked_DRM;
        g->compatibility_summary = "Blocked: Steamworks/DRM dependency detected (never bypassed).";
    } else if (report.architecture == PeArchitecture::X86_32) {
        g->compatibility = CompatibilityRating::Blocked_32Bit;
        g->compatibility_summary = "Blocked: 32-bit x86 binary requires WoW64/software JIT on Xbox UWP.";
    } else {
        g->compatibility = CompatibilityRating::InspectOnly;
        g->compatibility_summary =
            "PE metadata inspected (" + g->pe_architecture + ", " + g->pe_graphics_api +
            "). Execution unimplemented (Phase 7 pending).";
    }

    return report;
}

std::vector<std::filesystem::path> LibraryManager::discover_executables(const std::filesystem::path& dir,
                                                                        size_t max_results) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return found;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (found.size() >= max_results) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = to_lower(entry.path().extension().string());
        if (ext == ".exe") {
            found.push_back(entry.path());
        }
    }
    return found;
}

std::optional<std::filesystem::path> LibraryManager::ensure_save_directory(
    const std::filesystem::path& saves_root, const std::string& game_id) const {
    const std::string safe = sanitize_id(game_id);
    if (safe.empty()) return std::nullopt;
    const auto target = saves_root / safe;
    std::error_code ec;
    std::filesystem::create_directories(saves_root, ec);
    if (ec) return std::nullopt;
    if (!is_path_within_root(saves_root, target)) return std::nullopt;
    std::filesystem::create_directories(target, ec);
    if (ec) return std::nullopt;
    return target;
}

void LibraryManager::seed_demo_fixtures() {
    if (!games_.empty()) return;
    const uint64_t now = now_epoch_sec();

    auto make_fixture = [&](std::string id, std::string title, std::string dev, std::string genre,
                            std::string desc, uint32_t accent, uint64_t size_mb, bool installed,
                            bool fav, bool drm_free, CompatibilityRating rating,
                            std::string arch, std::string gfx, std::string summary,
                            std::vector<std::string> blockers, uint64_t played_ago, uint32_t steam_id) {
        GameEntry g;
        g.id = std::move(id);
        g.title = std::move(title);
        g.developer = std::move(dev);
        g.publisher = g.developer;
        g.genre = std::move(genre);
        g.description = std::move(desc);
        g.source = "SampleFixture";
        g.steam_app_id = steam_id;
        g.accent_color_rgb = accent;
        g.size_on_disk_bytes = size_mb * 1024ULL * 1024ULL;
        g.installed = installed;
        g.favorite = fav;
        g.drm_free_declared = drm_free;
        g.added_epoch_sec = now - played_ago - 3600;
        g.last_played_epoch_sec = played_ago > 0 ? (now - played_ago) : 0;
        g.compatibility = rating;
        g.compatibility_summary = std::move(summary);
        g.blockers = std::move(blockers);
        g.pe_inspected = true;
        g.pe_architecture = std::move(arch);
        g.pe_format = (g.pe_architecture.find("64") != std::string::npos) ? "PE32+" : "PE32";
        g.pe_subsystem = "Windows GUI";
        g.pe_graphics_api = std::move(gfx);
        g.pe_imported_dlls = {"KERNEL32.dll", "USER32.dll"};
        if (g.pe_graphics_api == "Direct3D 12") g.pe_imported_dlls.push_back("d3d12.dll");
        else if (g.pe_graphics_api == "Direct3D 11") g.pe_imported_dlls.push_back("d3d11.dll");
        else if (g.pe_graphics_api == "Direct3D 9") g.pe_imported_dlls.push_back("d3d9.dll");
        else if (g.pe_graphics_api == "Vulkan") g.pe_imported_dlls.push_back("vulkan-1.dll");
        g.pe_import_count = static_cast<uint32_t>(g.pe_imported_dlls.size());
        add_or_update_game(g);
    };

    make_fixture(
        "xwine-phase7-probe", "XWine Phase-7 DX12 Probe", "Project XWine Lab", "Diagnostic Target",
        "Planned original 64-bit DRM-free Windows test binary for Phase 7 validation: minimal kernel32 + D3D12 swapchain render test.",
        0x22D3EE, 4, true, true, true, CompatibilityRating::InspectOnly,
        "x86-64 (64-bit)", "Direct3D 12",
        "Primary Phase 7 test target. Not yet compiled or executed on physical Xbox Series S.",
        {"Requires Phase 6 Xbox UWP deployment", "Requires Phase 7 VirtualAllocFromApp PE loader"},
        1800, 0);

    make_fixture(
        "sample-indie-dx11", "Hollow Lantern (DRM-Free Fixture)", "Sample Indie Studio", "2D Action",
        "Sample catalog metadata representing a lightweight 64-bit DRM-free Direct3D 11 indie title (~1.4 GB footprint).",
        0x38BDF8, 1420, true, true, true, CompatibilityRating::InspectOnly,
        "x86-64 (64-bit)", "Direct3D 11",
        "Favorable memory footprint (< 5 GB UWP cap) and native D3D11 target; execution layer unimplemented.",
        {"Needs user32.dll virtual window shim", "Needs DXGI CoreWindow swapchain adapter", "Execution unverified"},
        7200, 0);

    make_fixture(
        "sample-classic-dx9", "Cobalt Runner 2008 (Fixture)", "Sample Legacy Works", "Arcade",
        "Sample metadata illustrating a legacy 32-bit Direct3D 9 title demonstrating dual WoW64 + D3D9On12 blockers.",
        0x6366F1, 980, true, false, true, CompatibilityRating::Blocked_32Bit,
        "x86 (32-bit)", "Direct3D 9",
        "Blocked by 32-bit x86 architecture (no WoW64 in Xbox UWP) and lack of native D3D9.",
        {"32-bit PE32 requires software x86->x64 JIT", "d3d9.dll requires D3D9On12 mapping layer"},
        14400, 0);

    make_fixture(
        "sample-vulkan-sim", "Orbital Forge (Vulkan Fixture)", "Sample Tech Labs", "Simulation",
        "Sample metadata illustrating a 64-bit Vulkan game blocked by the absence of a native Vulkan ICD on Xbox OS.",
        0x0EA5E9, 3200, true, true, true, CompatibilityRating::Blocked_MissingRuntime,
        "x86-64 (64-bit)", "Vulkan",
        "Blocked: Xbox OS provides no Vulkan ICD; Mesa Dozen translation is experimental.",
        {"No native Vulkan ICD on Xbox Series S", "Requires experimental Mesa Dozen (Vulkan-on-D3D12)"},
        0, 0);

    make_fixture(
        "sample-steam-drm", "Cyber Drift Arena (Steam DRM Fixture)", "Sample Commercial Studio", "Racing",
        "Sample metadata illustrating Steamworks DRM and multi-process Steam client dependency.",
        0xF59E0B, 14800, false, false, false, CompatibilityRating::Blocked_DRM,
        "x86-64 (64-bit)", "Direct3D 12",
        "Blocked: Requires Steam client IPC / Steamworks DRM. XWine never bypasses DRM.",
        {"Imports steam_api64.dll", "Steam client cannot run inside Xbox UWP AppContainer"},
        0, 1091500);

    make_fixture(
        "sample-anticheat", "Apex Vanguard Online (Anti-Cheat Fixture)", "Sample Online Corp", "Multiplayer",
        "Sample metadata illustrating kernel-mode anti-cheat incompatibility in Xbox Developer Mode.",
        0xEF4444, 42500, false, false, false, CompatibilityRating::Blocked_AntiCheat,
        "x86-64 (64-bit)", "Direct3D 12",
        "Technically Unsupported: Kernel anti-cheat drivers cannot load in Xbox Dev Mode UWP.",
        {"Easy Anti-Cheat kernel driver unsupported in UWP", "Exceeds 5 GB UWP Game Mode RAM budget"},
        0, 1172470);
}

bool LibraryManager::load_from_file(const std::filesystem::path& path, std::string* error_out) {
    std::ifstream in(path);
    if (!in.is_open()) {
        if (error_out) *error_out = "Unable to open library file: " + path.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = platform::JsonValue::parse(ss.str());
    if (!parsed.ok || !parsed.value.is_object()) {
        if (error_out) *error_out = "Invalid library JSON: " + parsed.error;
        return false;
    }

    games_.clear();
    const auto* arr = parsed.value.get("games");
    if (!arr || !arr->is_array()) return true;

    for (const auto& item : arr->as_array()) {
        if (!item.is_object()) continue;
        GameEntry g;
        if (auto* v = item.get("id")) g.id = v->as_string();
        if (auto* v = item.get("title")) g.title = v->as_string();
        if (auto* v = item.get("developer")) g.developer = v->as_string();
        if (auto* v = item.get("publisher")) g.publisher = v->as_string();
        if (auto* v = item.get("description")) g.description = v->as_string();
        if (auto* v = item.get("genre")) g.genre = v->as_string();
        if (auto* v = item.get("source")) g.source = v->as_string();
        if (auto* v = item.get("steam_app_id")) g.steam_app_id = static_cast<uint32_t>(v->as_uint());
        if (auto* v = item.get("install_directory")) g.install_directory = v->as_string();
        if (auto* v = item.get("executable_relative_path")) g.executable_relative_path = v->as_string();
        if (auto* v = item.get("cover_image_path")) g.cover_image_path = v->as_string();
        if (auto* v = item.get("accent_color_rgb")) g.accent_color_rgb = static_cast<uint32_t>(v->as_uint(0x0EA5E9));
        if (auto* v = item.get("size_on_disk_bytes")) g.size_on_disk_bytes = v->as_uint();
        if (auto* v = item.get("installed")) g.installed = v->as_bool();
        if (auto* v = item.get("favorite")) g.favorite = v->as_bool();
        if (auto* v = item.get("drm_free_declared")) g.drm_free_declared = v->as_bool();
        if (auto* v = item.get("added_epoch_sec")) g.added_epoch_sec = v->as_uint();
        if (auto* v = item.get("last_played_epoch_sec")) g.last_played_epoch_sec = v->as_uint();
        if (auto* v = item.get("play_count")) g.play_count = static_cast<uint32_t>(v->as_uint());
        if (auto* v = item.get("compatibility")) g.compatibility = parse_compatibility_rating(v->as_string());
        if (auto* v = item.get("compatibility_summary")) g.compatibility_summary = v->as_string();
        if (auto* v = item.get("pe_inspected")) g.pe_inspected = v->as_bool();
        if (auto* v = item.get("pe_architecture")) g.pe_architecture = v->as_string();
        if (auto* v = item.get("pe_format")) g.pe_format = v->as_string();
        if (auto* v = item.get("pe_subsystem")) g.pe_subsystem = v->as_string();
        if (auto* v = item.get("pe_graphics_api")) g.pe_graphics_api = v->as_string();
        if (auto* v = item.get("pe_import_count")) g.pe_import_count = static_cast<uint32_t>(v->as_uint());
        if (auto* v = item.get("executable_checksum_fnv1a")) g.executable_checksum_fnv1a = v->as_string();

        if (auto* b = item.get("blockers"); b && b->is_array()) {
            for (const auto& s : b->as_array()) g.blockers.push_back(s.as_string());
        }
        if (auto* d = item.get("pe_imported_dlls"); d && d->is_array()) {
            for (const auto& s : d->as_array()) g.pe_imported_dlls.push_back(s.as_string());
        }
        if (auto* lo = item.get("launch_options"); lo && lo->is_object()) {
            if (auto* v = lo->get("arguments")) g.launch_options.arguments = v->as_string();
            if (auto* v = lo->get("runtime_profile")) g.launch_options.runtime_profile = v->as_string_or("InspectOnly");
            if (auto* v = lo->get("graphics_backend")) g.launch_options.graphics_backend = v->as_string_or("Auto");
            if (auto* v = lo->get("controller_profile")) g.launch_options.controller_profile = v->as_string_or("XboxStandard");
            if (auto* v = lo->get("enable_logging")) g.launch_options.enable_logging = v->as_bool(true);
        }
        add_or_update_game(g);
    }
    return true;
}

bool LibraryManager::save_to_file(const std::filesystem::path& path, std::string* error_out) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error_out) *error_out = "Failed to create library parent directory: " + ec.message();
            return false;
        }
    }

    platform::JsonValue root = platform::JsonValue::make_object();
    root["schema_version"] = static_cast<uint64_t>(1);
    platform::JsonValue arr = platform::JsonValue::make_array();

    for (const auto& g : games_) {
        platform::JsonValue o = platform::JsonValue::make_object();
        o["id"] = g.id;
        o["title"] = g.title;
        o["developer"] = g.developer;
        o["publisher"] = g.publisher;
        o["description"] = g.description;
        o["genre"] = g.genre;
        o["source"] = g.source;
        o["steam_app_id"] = static_cast<uint64_t>(g.steam_app_id);
        o["install_directory"] = g.install_directory;
        o["executable_relative_path"] = g.executable_relative_path;
        o["cover_image_path"] = g.cover_image_path;
        o["accent_color_rgb"] = static_cast<uint64_t>(g.accent_color_rgb);
        o["size_on_disk_bytes"] = g.size_on_disk_bytes;
        o["installed"] = g.installed;
        o["favorite"] = g.favorite;
        o["drm_free_declared"] = g.drm_free_declared;
        o["added_epoch_sec"] = g.added_epoch_sec;
        o["last_played_epoch_sec"] = g.last_played_epoch_sec;
        o["play_count"] = static_cast<uint64_t>(g.play_count);
        o["compatibility"] = to_string(g.compatibility);
        o["compatibility_summary"] = g.compatibility_summary;
        o["pe_inspected"] = g.pe_inspected;
        o["pe_architecture"] = g.pe_architecture;
        o["pe_format"] = g.pe_format;
        o["pe_subsystem"] = g.pe_subsystem;
        o["pe_graphics_api"] = g.pe_graphics_api;
        o["pe_import_count"] = static_cast<uint64_t>(g.pe_import_count);
        o["executable_checksum_fnv1a"] = g.executable_checksum_fnv1a;

        platform::JsonValue blockers = platform::JsonValue::make_array();
        for (const auto& b : g.blockers) blockers.push_back(b);
        o["blockers"] = std::move(blockers);

        platform::JsonValue dlls = platform::JsonValue::make_array();
        for (const auto& d : g.pe_imported_dlls) dlls.push_back(d);
        o["pe_imported_dlls"] = std::move(dlls);

        platform::JsonValue lo = platform::JsonValue::make_object();
        lo["arguments"] = g.launch_options.arguments;
        lo["runtime_profile"] = g.launch_options.runtime_profile;
        lo["graphics_backend"] = g.launch_options.graphics_backend;
        lo["controller_profile"] = g.launch_options.controller_profile;
        lo["enable_logging"] = g.launch_options.enable_logging;
        o["launch_options"] = std::move(lo);

        arr.push_back(std::move(o));
    }

    root["games"] = std::move(arr);
    const std::filesystem::path tmp_path = path.string() + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            if (error_out) *error_out = "Could not open temp library file for write.";
            return false;
        }
        out << root.dump(2) << "\n";
        if (!out.good()) {
            if (error_out) *error_out = "Failed writing library JSON.";
            return false;
        }
    }
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        if (error_out) *error_out = "Atomic rename failed for library JSON: " + ec.message();
        return false;
    }
    return true;
}

} // namespace xwine::runtime
