#include "xwine/runtime/runtime_manager.hpp"
#include "xwine/platform/logger.hpp"

namespace xwine::runtime {

RuntimeManager::RuntimeManager() {
    backends_ = {
        {
            "xwine-pe-inspector",
            "XWine Static PE32/PE32+ Inspector",
            "0.2.0",
            platform::CapabilityStatus::Implemented,
            true,
            true,
            "Safely parses Windows PE headers, sections, entropy, and import tables without executing code.",
            ""
        },
        {
            "xwine-onecoreuap-loader",
            "XWine Win32-on-OneCoreUAP Loader (x86-64)",
            "0.0.1-design",
            platform::CapabilityStatus::Planned,
            false,
            true,
            "Planned Phase 7 in-process x86-64 PE mapper using VirtualAllocFromApp + VirtualProtectFromApp (W^X).",
            "Requires Phase 4 minimal Win32 shims and Phase 6 physical Xbox Series S UWP deployment."
        },
        {
            "wine-upstream",
            "Upstream Wine (gitlab.winehq.org/wine/wine)",
            "9.x / 10.x (Research Reference)",
            platform::CapabilityStatus::TechnicallyUnsupported,
            false,
            false,
            "Studied for Win32 DLL specs and loader semantics; cannot build or run unmodified as an Xbox UWP package.",
            "Depends on POSIX syscalls (ntdll/unix), Unix domain sockets, fork/execve, and multi-process wineserver."
        },
        {
            "proton-steam-runtime",
            "Valve Proton / Steam Runtime",
            "Research Reference",
            platform::CapabilityStatus::TechnicallyUnsupported,
            false,
            false,
            "Studied for Steam metadata and compatibility taxonomy; Linux container runtime is non-portable to Xbox OS.",
            "Requires Linux bubblewrap/pressure-vessel, Vulkan ICD (DXVK/VKD3D-Proton), and live Steam client."
        }
    };

    // Seed baseline virtual Windows environment & registry keys
    env_vars_["OS"] = "Windows_NT";
    env_vars_["PROCESSOR_ARCHITECTURE"] = "AMD64";
    env_vars_["SYSTEMROOT"] = "C:\\Windows";
    env_vars_["WINDIR"] = "C:\\Windows";
    env_vars_["USERPROFILE"] = "C:\\Users\\XWine";
    env_vars_["LOCALAPPDATA"] = "C:\\Users\\XWine\\AppData\\Local";

    set_virtual_registry_value("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductName",
                               "XWine Virtual Environment (Experimental)");
    set_virtual_registry_value("HKLM\\SOFTWARE\\XWine", "ExecutionStatus", "NotImplemented_InspectionOnly");
}

void RuntimeManager::set_virtual_registry_value(const std::string& key_path, const std::string& value_name,
                                                const std::string& data) {
    virtual_registry_[key_path][value_name] = data;
}

std::optional<std::string> RuntimeManager::get_virtual_registry_value(const std::string& key_path,
                                                                      const std::string& value_name) const {
    auto kit = virtual_registry_.find(key_path);
    if (kit == virtual_registry_.end()) return std::nullopt;
    auto vit = kit->second.find(value_name);
    if (vit == kit->second.end()) return std::nullopt;
    return vit->second;
}

size_t RuntimeManager::virtual_registry_entry_count() const {
    size_t count = 0;
    for (const auto& [_, values] : virtual_registry_) {
        count += values.size();
    }
    return count;
}

void RuntimeManager::set_env_var(const std::string& name, const std::string& value) {
    env_vars_[name] = value;
}

std::optional<std::string> RuntimeManager::get_env_var(const std::string& name) const {
    auto it = env_vars_.find(name);
    if (it == env_vars_.end()) return std::nullopt;
    return it->second;
}

LaunchAttemptReport RuntimeManager::evaluate_launch_request(const GameEntry& game,
                                                            platform::PlatformId host_platform) const {
    LaunchAttemptReport out;
    out.executed = false; // Honest invariant: Phase 2/3 never pretends to run Windows games.
    out.game_id = game.id;

    auto& log = platform::Logger::instance();
    log.info("Runtime", "Evaluating launch feasibility for '" + game.title + "' on " +
                            platform::to_string(host_platform));

    if (!game.install_directory.empty() && !game.executable_relative_path.empty()) {
        std::filesystem::path exe = std::filesystem::path(game.install_directory) / game.executable_relative_path;
        out.pe_report = PeInspector::inspect_file(exe);
        if (out.pe_report.valid_pe) {
            out.blockers.insert(out.blockers.end(),
                                out.pe_report.xbox_blockers.begin(),
                                out.pe_report.xbox_blockers.end());
        } else {
            out.blockers.push_back("PE Inspection failed: " + out.pe_report.error_message);
        }
    } else {
        out.blockers = game.blockers;
        out.blockers.push_back("No local Windows executable attached (catalog metadata fixture only).");
    }

    const auto proc_decision = process_policy_.evaluate_child_process_request(
        game.executable_relative_path.empty() ? game.title : game.executable_relative_path,
        host_platform == platform::PlatformId::XboxSeriesS_UWP_GameMode ||
            host_platform == platform::PlatformId::XboxSeriesS_UWP_AppMode);
    if (!proc_decision.permitted) {
        out.blockers.push_back(proc_decision.reason);
    }

    if (host_platform == platform::PlatformId::MacOS_Arm64) {
        out.blockers.push_back(
            "Apple Silicon macOS (ARM64) is the development workstation; Windows x86-64 execution is not executed here.");
    }

    out.summary = "Execution blocked honestly: " + std::to_string(out.blockers.size()) +
                  " technical blocker(s) identified. XWine is in Phase 2/3 (inspection & architecture baseline).";
    log.warn("Runtime", out.summary);
    return out;
}

} // namespace xwine::runtime
