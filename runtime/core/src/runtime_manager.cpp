#include "xwine/runtime/runtime_manager.hpp"
#include "xwine/platform/logger.hpp"
#include "xwine/platform/xbox_capabilities.hpp"

namespace xwine::runtime {

RuntimeManager::RuntimeManager(const std::filesystem::path& prefix_root) {
    const auto effective_prefix =
        prefix_root.empty() ? (std::filesystem::temp_directory_path() / "xwine_prefix") : prefix_root;
    shims_ = std::make_unique<Win32ShimEnvironment>(effective_prefix);

    backends_ = {
        {
            "xwine-pe-loader",
            "XWine PE32+ Inspector, Relocation & IAT Mapper",
            "0.3.0",
            platform::CapabilityStatus::Implemented,
            true,
            true,
            "Parses PE32/PE32+, exports, .reloc, delay-imports & TLS; maps sections, applies DIR64 fixups, binds Phase 4 shims, enforces W^X.",
            ""
        },
        {
            "xwine-onecoreuap-shims",
            "XWine Win32-to-OneCoreUAP Minimal Shim Suite",
            "0.3.0",
            platform::CapabilityStatus::Implemented,
            true,
            true,
            "Unit-tested in-process kernel32 (file/heap/QPC/debug), user32 (virtual HWND/queue), advapi32 (virtual registry), and XInput shims.",
            "On-console execution via VirtualAllocFromApp awaits Phase 6 Windows SDK MSIX build & Xbox Series S hardware test."
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

    env_vars_["OS"] = "Windows_NT";
    env_vars_["PROCESSOR_ARCHITECTURE"] = "AMD64";
    env_vars_["SYSTEMROOT"] = "C:\\Windows";
    env_vars_["WINDIR"] = "C:\\Windows";
    env_vars_["USERPROFILE"] = "C:\\Users\\XWine";
    env_vars_["LOCALAPPDATA"] = "C:\\Users\\XWine\\AppData\\Local";
}

void RuntimeManager::configure_prefix(const std::filesystem::path& prefix_root) {
    if (prefix_root.empty()) return;
    shims_ = std::make_unique<Win32ShimEnvironment>(prefix_root);
    const auto reg_file = prefix_root / "registry.json";
    if (std::filesystem::exists(reg_file)) {
        shims_->virtual_registry().load_from_file(reg_file);
    } else {
        shims_->virtual_registry().save_to_file(reg_file);
    }
}

void RuntimeManager::set_virtual_registry_value(const std::string& key_path, const std::string& value_name,
                                                const std::string& data) {
    shims_->virtual_registry().set_value(key_path, value_name, RegValue::make_string(data));
}

std::optional<std::string> RuntimeManager::get_virtual_registry_value(const std::string& key_path,
                                                                      const std::string& value_name) const {
    auto v = shims_->virtual_registry().get_value(key_path, value_name);
    if (!v || v->type != RegValueType::String) return std::nullopt;
    return std::get<std::string>(v->data);
}

size_t RuntimeManager::virtual_registry_entry_count() const {
    return shims_->virtual_registry().total_value_count();
}

void RuntimeManager::set_env_var(const std::string& name, const std::string& value) {
    env_vars_[name] = value;
    shims_->set_env(name, value);
}

std::optional<std::string> RuntimeManager::get_env_var(const std::string& name) const {
    auto it = env_vars_.find(name);
    if (it == env_vars_.end()) return std::nullopt;
    return it->second;
}

LaunchAttemptReport RuntimeManager::evaluate_launch_request(const GameEntry& game,
                                                            platform::PlatformId host_platform) const {
    LaunchAttemptReport out;
    out.executed = false;
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

            // Run safe non-executing Phase 3/4 dry-run mapping to report exact missing APIs
            if (out.pe_report.format == PeFormat::PE32Plus &&
                out.pe_report.architecture == PeArchitecture::X86_64) {
                DryRunMemoryAllocator dry_alloc(platform::kXboxDevModeGameRamCapBytes);
                Win32ShimEnvironment dry_shims(shims_->path_virtualizer().prefix_root());
                auto mapped = PeImageMapper::map_pe_file(exe, dry_alloc, dry_shims);
                out.dry_run_mapped = mapped.mapped;
                out.relocations_applied = mapped.relocations_applied;
                out.shims_resolved = mapped.imports_resolved;
                out.shims_unresolved = mapped.imports_unresolved;
                out.unresolved_symbols = mapped.unresolved_symbols;
                if (mapped.mapped) {
                    dry_alloc.release_image(mapped.base_address, mapped.image_size);
                } else if (!mapped.error_message.empty()) {
                    out.blockers.push_back("PE Dry-Run Mapper: " + mapped.error_message);
                }
            }
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
            "Apple Silicon macOS (ARM64) is the development workstation; Windows x86-64 binaries execute only on Windows/Xbox x64.");
    }

    out.summary = "Dry-run evaluated: " + std::to_string(out.shims_resolved) + " shim(s) resolved, " +
                  std::to_string(out.shims_unresolved) + " missing symbol(s), " +
                  std::to_string(out.blockers.size()) + " blocker(s). Executed: false.";
    log.warn("Runtime", out.summary);
    return out;
}

LaunchAttemptReport RuntimeManager::run_phase7_probe(const std::filesystem::path& exe_path,
                                                     uint64_t memory_budget_bytes) {
    LaunchAttemptReport out;
    out.game_id = exe_path.filename().string();
    out.pe_report = PeInspector::inspect_file(exe_path);
    if (!out.pe_report.valid_pe) {
        out.summary = "Probe aborted: invalid PE image.";
        out.blockers.push_back(out.pe_report.error_message);
        return out;
    }

    auto allocator = create_platform_pe_allocator(memory_budget_bytes);
    auto mapped = PeImageMapper::map_pe_file(exe_path, *allocator, *shims_);
    out.dry_run_mapped = mapped.mapped;
    out.relocations_applied = mapped.relocations_applied;
    out.shims_resolved = mapped.imports_resolved;
    out.shims_unresolved = mapped.imports_unresolved;
    out.unresolved_symbols = mapped.unresolved_symbols;

    if (!mapped.mapped) {
        out.summary = "PE mapping failed: " + mapped.error_message;
        out.blockers.push_back(mapped.error_message);
        return out;
    }

    auto exec_res = PeImageMapper::execute_mapped_x64(mapped, *allocator, *shims_);
    out.executed = exec_res.executed;
    out.exit_code = exec_res.exit_code;
    out.summary = exec_res.status_message;
    if (!exec_res.executed) {
        out.blockers.push_back(exec_res.status_message);
    }
    allocator->release_image(mapped.base_address, mapped.image_size);
    return out;
}

} // namespace xwine::runtime
