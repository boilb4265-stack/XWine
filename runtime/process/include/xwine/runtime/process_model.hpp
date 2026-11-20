#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xwine::runtime {

enum class ProcessExecutionState {
    NotStarted,
    InspectingOnly,
    BlockedByPlatformPolicy,
    Terminated
};

struct ProcessDescriptor {
    uint32_t virtual_pid = 0;
    std::string image_name;
    std::string working_directory;
    std::vector<std::string> arguments;
    ProcessExecutionState state = ProcessExecutionState::NotStarted;
    std::string status_reason;
};

// Models Xbox UWP AppContainer's single-process constraint (no arbitrary CreateProcess).
class ProcessPolicyController {
public:
    struct SpawnDecision {
        bool permitted = false;
        std::string reason;
    };

    SpawnDecision evaluate_child_process_request(const std::string& executable_name,
                                                 bool is_xbox_uwp_target) const;
};

} // namespace xwine::runtime
