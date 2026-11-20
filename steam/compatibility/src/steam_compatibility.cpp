#include "xwine/steam/steam_compatibility.hpp"

#include <algorithm>
#include <cctype>

namespace xwine::steam {

namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

SteamCompatibilityAssessment SteamCompatibilityEvaluator::evaluate(
    const SteamAppManifest& manifest,
    const std::optional<runtime::PeInspectionReport>& pe_report) {
    SteamCompatibilityAssessment res;

    // Metadata import NEVER equals executable compatibility.
    res.blockers.push_back(
        "Steam metadata imported only: Downloading/importing Steam manifests does not make a title executable on Xbox.");

    if (manifest.size_on_disk_bytes > (35ULL * 1024ULL * 1024ULL * 1024ULL)) {
        res.exceeds_xbox_5gb_cap_hint = true;
        res.blockers.push_back(
            "Large install footprint (>35 GB on disk): High risk of exceeding Xbox Dev Mode 5 GB unified RAM+VRAM cap.");
    }

    if (pe_report && pe_report->valid_pe) {
        for (const auto& imp : pe_report->imports) {
            const std::string dll = to_lower(imp.dll_name);
            if (dll.find("steam_api") != std::string::npos) {
                res.likely_steam_drm = true;
            }
            if (dll.find("easyanticheat") != std::string::npos ||
                dll.find("battleye") != std::string::npos ||
                dll.find("beclient") != std::string::npos) {
                res.detected_anti_cheat = true;
            }
        }
        for (const auto& b : pe_report->xbox_blockers) {
            res.blockers.push_back(b);
        }

        if (res.detected_anti_cheat) {
            res.rating = runtime::CompatibilityRating::Blocked_AntiCheat;
            res.summary = "Blocked: Kernel anti-cheat detected in Steam title binary.";
        } else if (res.likely_steam_drm) {
            res.rating = runtime::CompatibilityRating::Blocked_DRM;
            res.summary = "Blocked: Imports Steamworks API (DRM/client IPC unsupported in Xbox UWP; never bypassed).";
        } else if (pe_report->architecture == runtime::PeArchitecture::X86_32) {
            res.rating = runtime::CompatibilityRating::Blocked_32Bit;
            res.summary = "Blocked: 32-bit x86 binary requires WoW64/software JIT on Xbox UWP.";
        } else {
            res.rating = runtime::CompatibilityRating::InspectOnly;
            res.summary = "Metadata & PE inspected (" + runtime::to_string(pe_report->architecture) +
                          "). Execution unverified/unimplemented.";
        }
    } else {
        res.rating = runtime::CompatibilityRating::Untested;
        res.summary = "Steam manifest imported (no Windows PE binary inspected locally).";
        res.blockers.push_back("Steam client runtime (steam.exe / CEF) cannot run inside Xbox UWP AppContainer.");
    }

    return res;
}

} // namespace xwine::steam
