#pragma once

#include "xwine/platform/types.hpp"
#include "xwine/runtime/api_catalog.hpp"
#include "xwine/runtime/library_manager.hpp"
#include "xwine/runtime/pe_inspector.hpp"
#include "xwine/runtime/pe_loader.hpp"
#include "xwine/runtime/process_model.hpp"
#include "xwine/runtime/virtual_memory.hpp"
#include "xwine/runtime/win32_shims.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xwine::runtime {

struct RuntimeBackendDescriptor {
    std::string id;
    std::string name;
    std::string version;
    platform::CapabilityStatus status;
    bool supports_macos_host = false;
    bool supports_xbox_uwp = false;
    std::string summary;
    std::string blocker;
};

struct LaunchAttemptReport {
    bool executed = false; // True ONLY when Phase 7 x86-64 execution actually ran on Windows/Xbox
    int32_t exit_code = -1;
    std::string game_id;
    std::string summary;
    std::vector<std::string> blockers;
    PeInspectionReport pe_report;

    // Phase 3 & 4 mapping diagnostics
    bool dry_run_mapped = false;
    uint32_t relocations_applied = 0;
    uint32_t shims_resolved = 0;
    uint32_t shims_unresolved = 0;
    std::vector<std::string> unresolved_symbols;
};

class RuntimeManager {
public:
    explicit RuntimeManager(const std::filesystem::path& prefix_root = {});

    void configure_prefix(const std::filesystem::path& prefix_root);

    const std::vector<RuntimeBackendDescriptor>& backends() const { return backends_; }
    const WindowsApiCatalog& api_catalog() const { return api_catalog_; }
    const ProcessPolicyController& process_policy() const { return process_policy_; }
    Win32ShimEnvironment& shim_environment() { return *shims_; }
    const Win32ShimEnvironment& shim_environment() const { return *shims_; }

    // Persistent Virtual Registry forwarding
    void set_virtual_registry_value(const std::string& key_path, const std::string& value_name,
                                    const std::string& data);
    std::optional<std::string> get_virtual_registry_value(const std::string& key_path,
                                                          const std::string& value_name) const;
    size_t virtual_registry_entry_count() const;

    // Virtual Windows Environment Variables
    void set_env_var(const std::string& name, const std::string& value);
    std::optional<std::string> get_env_var(const std::string& name) const;
    const std::map<std::string, std::string>& env_vars() const { return env_vars_; }

    // Evaluates launching a game honestly: performs static PE inspection, Phase 3/4 dry-run
    // section/reloc/IAT mapping against Win32ShimEnvironment, logs blockers, and NEVER fakes execution.
    LaunchAttemptReport evaluate_launch_request(const GameEntry& game,
                                                platform::PlatformId host_platform) const;

    // Experimental Phase 7 runner: maps and executes an original DRM-free x86-64 PE32+ binary
    // ONLY on Windows/Xbox x64 hardware when all imports resolve to implemented Phase 4 shims.
    LaunchAttemptReport run_phase7_probe(const std::filesystem::path& exe_path,
                                         uint64_t memory_budget_bytes);

private:
    std::vector<RuntimeBackendDescriptor> backends_;
    WindowsApiCatalog api_catalog_;
    ProcessPolicyController process_policy_;
    std::unique_ptr<Win32ShimEnvironment> shims_;
    std::map<std::string, std::string> env_vars_;
};

} // namespace xwine::runtime
