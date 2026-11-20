#pragma once

#include "xwine/platform/types.hpp"
#include "xwine/runtime/api_catalog.hpp"
#include "xwine/runtime/library_manager.hpp"
#include "xwine/runtime/virtual_memory.hpp"
#include "xwine/runtime/pe_inspector.hpp"
#include "xwine/runtime/process_model.hpp"

#include <map>
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
    bool executed = false; // ALWAYS false until Phase 7 real execution exists!
    std::string game_id;
    std::string summary;
    std::vector<std::string> blockers;
    PeInspectionReport pe_report;
};

class RuntimeManager {
public:
    RuntimeManager();

    const std::vector<RuntimeBackendDescriptor>& backends() const { return backends_; }
    const WindowsApiCatalog& api_catalog() const { return api_catalog_; }
    const ProcessPolicyController& process_policy() const { return process_policy_; }

    // Virtual Windows Registry (in-process key/value store avoiding blocked HKLM/HKCU OS writes)
    void set_virtual_registry_value(const std::string& key_path, const std::string& value_name,
                                    const std::string& data);
    std::optional<std::string> get_virtual_registry_value(const std::string& key_path,
                                                          const std::string& value_name) const;
    size_t virtual_registry_entry_count() const;

    // Virtual Windows Environment Variables
    void set_env_var(const std::string& name, const std::string& value);
    std::optional<std::string> get_env_var(const std::string& name) const;
    const std::map<std::string, std::string>& env_vars() const { return env_vars_; }

    // Evaluates launching a game honestly: performs static PE inspection + policy checks,
    // logs exact blockers, and NEVER fakes successful game execution.
    LaunchAttemptReport evaluate_launch_request(const GameEntry& game,
                                                platform::PlatformId host_platform) const;

private:
    std::vector<RuntimeBackendDescriptor> backends_;
    WindowsApiCatalog api_catalog_;
    ProcessPolicyController process_policy_;
    std::map<std::string, std::map<std::string, std::string>> virtual_registry_;
    std::map<std::string, std::string> env_vars_;
};

} // namespace xwine::runtime
