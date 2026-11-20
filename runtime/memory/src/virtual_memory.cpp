#include "xwine/runtime/virtual_memory.hpp"

#include <algorithm>

namespace xwine::runtime {

VirtualMemoryBudgetModel::VirtualMemoryBudgetModel(uint64_t budget_bytes, bool enforce_strict_wx)
    : budget_bytes_(budget_bytes), enforce_strict_wx_(enforce_strict_wx) {}

MemoryPolicyResult VirtualMemoryBudgetModel::reserve_and_commit(uint64_t base_address, uint64_t size_bytes,
                                                                MemoryProtection protection,
                                                                const std::string& tag) {
    if (size_bytes == 0) {
        return {false, "Allocation size must be non-zero."};
    }
    if (enforce_strict_wx_ && protection == MemoryProtection::ExecuteReadWrite) {
        return {false,
                "PAGE_EXECUTE_READWRITE blocked by UWP W^X policy; allocate PAGE_READWRITE via "
                "VirtualAllocFromApp then transition to PAGE_EXECUTE_READ via VirtualProtectFromApp."};
    }
    if (size_bytes > remaining_bytes()) {
        return {false, "Allocation exceeds Xbox Dev Mode UWP memory budget."};
    }

    VirtualRegion reg;
    reg.base_address = base_address;
    reg.size_bytes = size_bytes;
    reg.protection = protection;
    reg.tag = tag;
    regions_.push_back(std::move(reg));
    committed_bytes_ += size_bytes;
    return {true, "Committed within budget."};
}

MemoryPolicyResult VirtualMemoryBudgetModel::protect_region(uint64_t base_address, MemoryProtection new_protection) {
    if (enforce_strict_wx_ && new_protection == MemoryProtection::ExecuteReadWrite) {
        return {false, "Cannot transition region to RWX under strict UWP W^X policy."};
    }
    for (auto& r : regions_) {
        if (r.base_address == base_address) {
            r.protection = new_protection;
            if (new_protection == MemoryProtection::ExecuteRead) {
                r.executable_transitioned = true;
            }
            return {true, "Region protection updated."};
        }
    }
    return {false, "Virtual region not found at specified base address."};
}

bool VirtualMemoryBudgetModel::release_region(uint64_t base_address) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
                           [base_address](const VirtualRegion& r) { return r.base_address == base_address; });
    if (it == regions_.end()) return false;
    committed_bytes_ = (committed_bytes_ >= it->size_bytes) ? (committed_bytes_ - it->size_bytes) : 0;
    regions_.erase(it);
    return true;
}

} // namespace xwine::runtime
