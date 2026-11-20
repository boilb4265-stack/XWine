#include "xwine/runtime/process_model.hpp"

namespace xwine::runtime {

ProcessPolicyController::SpawnDecision ProcessPolicyController::evaluate_child_process_request(
    const std::string& executable_name, bool is_xbox_uwp_target) const {
    if (is_xbox_uwp_target) {
        return {false,
                "Xbox UWP AppContainer prohibits spawning arbitrary child Win32 executables ('" +
                    executable_name + "') via CreateProcess. Multi-process launchers and DRM helpers are unsupported."};
    }
    return {false,
            "Host process execution of Windows PE binary '" + executable_name +
                "' is disabled in Phase 2/3 inspection mode (no execution layer implemented)."};
}

} // namespace xwine::runtime
