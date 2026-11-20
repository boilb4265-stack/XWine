#pragma once

#include "xwine/runtime/pe_inspector.hpp"
#include "xwine/runtime/virtual_memory.hpp"
#include "xwine/runtime/win32_shims.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace xwine::runtime {

struct PeExportSymbol {
    std::string name;
    uint32_t ordinal = 0;
    uint32_t rva = 0;
};

struct PeRelocationEntry {
    uint32_t rva = 0;
    uint8_t type = 0; // 0 = ABSOLUTE, 3 = HIGHLOW (32-bit), 10 = DIR64 (64-bit)
};

struct PeExtendedDirectories {
    std::string export_dll_name;
    std::vector<PeExportSymbol> exports;
    std::vector<PeRelocationEntry> relocations;
    std::vector<std::string> delay_load_dlls;
    std::vector<uint64_t> tls_callback_vas;
};

// Abstract virtual memory allocator:
// - XboxUwpMemoryAllocator (Win32/Xbox UWP): calls VirtualAllocFromApp + VirtualProtectFromApp (W^X)
// - DryRunMemoryAllocator (macOS/tests): allocates safe RW memory buffer and tracks strict W^X state
class IVirtualMemoryAllocator {
public:
    virtual ~IVirtualMemoryAllocator() = default;
    virtual uint8_t* allocate_image(size_t size_bytes, std::string* error_out = nullptr) = 0;
    virtual bool protect_range(uint8_t* address, size_t size_bytes, MemoryProtection prot,
                               std::string* error_out = nullptr) = 0;
    virtual void release_image(uint8_t* base, size_t size_bytes) = 0;
    virtual bool supports_native_x64_execution() const = 0;
};

class DryRunMemoryAllocator final : public IVirtualMemoryAllocator {
public:
    explicit DryRunMemoryAllocator(uint64_t budget_bytes = 5120ULL * 1024ULL * 1024ULL);

    uint8_t* allocate_image(size_t size_bytes, std::string* error_out = nullptr) override;
    bool protect_range(uint8_t* address, size_t size_bytes, MemoryProtection prot,
                       std::string* error_out = nullptr) override;
    void release_image(uint8_t* base, size_t size_bytes) override;
    bool supports_native_x64_execution() const override { return false; }

    const std::vector<std::pair<uintptr_t, MemoryProtection>>& protection_log() const {
        return protection_log_;
    }

private:
    uint64_t budget_bytes_;
    std::vector<std::unique_ptr<uint8_t[]>> owned_blocks_;
    std::vector<std::pair<uintptr_t, MemoryProtection>> protection_log_;
};

std::unique_ptr<IVirtualMemoryAllocator> create_platform_pe_allocator(uint64_t budget_bytes);

struct MappedPeImage {
    bool mapped = false;
    bool ready_for_native_x64_execution = false;
    std::string error_message;

    uint8_t* base_address = nullptr;
    size_t image_size = 0;
    uint32_t entry_point_rva = 0;
    int64_t relocation_delta = 0;
    uint32_t relocations_applied = 0;
    uint32_t imports_resolved = 0;
    uint32_t imports_unresolved = 0;
    uint32_t wx_transitions_applied = 0;
    std::vector<std::string> unresolved_symbols;
    std::vector<std::string> resolved_shims;
    PeExtendedDirectories extended_dirs;
};

struct PeExecutionResult {
    bool executed = false;
    int32_t exit_code = -1;
    std::string status_message;
};

class PeImageMapper {
public:
    static PeExtendedDirectories parse_extended_directories(const std::vector<uint8_t>& raw_pe,
                                                            const PeInspectionReport& report);

    static MappedPeImage map_pe_bytes(const std::vector<uint8_t>& raw_pe,
                                      IVirtualMemoryAllocator& allocator,
                                      Win32ShimEnvironment& shims,
                                      const std::string& label = "<memory>");

    static MappedPeImage map_pe_file(const std::filesystem::path& path,
                                     IVirtualMemoryAllocator& allocator,
                                     Win32ShimEnvironment& shims);

    // Executes a mapped PE32+ image ONLY on x86-64 Windows/Xbox UWP when allocator supports native execution
    // and zero unresolved imports remain. On macOS, honestly returns executed = false.
    static PeExecutionResult execute_mapped_x64(const MappedPeImage& image,
                                                const IVirtualMemoryAllocator& allocator,
                                                Win32ShimEnvironment& shims);
};

} // namespace xwine::runtime
