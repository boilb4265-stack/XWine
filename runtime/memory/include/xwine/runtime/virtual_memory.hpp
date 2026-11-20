#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xwine::runtime {

enum class MemoryProtection {
    NoAccess,
    ReadOnly,
    ReadWrite,
    ExecuteRead,
    ExecuteReadWrite // Disallowed under strict UWP W^X policy
};

struct VirtualRegion {
    uint64_t base_address = 0;
    uint64_t size_bytes = 0;
    MemoryProtection protection = MemoryProtection::ReadWrite;
    std::string tag;
    bool executable_transitioned = false;
};

struct MemoryPolicyResult {
    bool allowed = false;
    std::string reason;
};

// Models Xbox UWP Dev Mode virtual memory limits (5,120 MiB Game Mode / 1,024 MiB App Mode)
// and enforces strict W^X (VirtualAllocFromApp RW -> VirtualProtectFromApp RX) rules.
class VirtualMemoryBudgetModel {
public:
    explicit VirtualMemoryBudgetModel(uint64_t budget_bytes, bool enforce_strict_wx = true);

    uint64_t budget_bytes() const { return budget_bytes_; }
    uint64_t committed_bytes() const { return committed_bytes_; }
    uint64_t remaining_bytes() const { return committed_bytes_ >= budget_bytes_ ? 0 : budget_bytes_ - committed_bytes_; }
    bool strict_wx_enabled() const { return enforce_strict_wx_; }

    MemoryPolicyResult reserve_and_commit(uint64_t base_address, uint64_t size_bytes,
                                          MemoryProtection protection, const std::string& tag);
    MemoryPolicyResult protect_region(uint64_t base_address, MemoryProtection new_protection);
    bool release_region(uint64_t base_address);

    const std::vector<VirtualRegion>& regions() const { return regions_; }

private:
    uint64_t budget_bytes_;
    uint64_t committed_bytes_ = 0;
    bool enforce_strict_wx_;
    std::vector<VirtualRegion> regions_;
};

} // namespace xwine::runtime
