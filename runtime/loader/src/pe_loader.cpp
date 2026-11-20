#include "xwine/runtime/pe_loader.hpp"
#include "xwine/platform/logger.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xwine::runtime {

namespace {

constexpr size_t kDirExport      = 0;
constexpr size_t kDirImport      = 1;
constexpr size_t kDirBaseReloc   = 5;
constexpr size_t kDirTls         = 9;
constexpr size_t kDirDelayImport = 13;

template <typename T>
bool read_le(const std::vector<uint8_t>& buf, size_t offset, T& out) {
    if (offset > buf.size() || sizeof(T) > buf.size() - offset) return false;
    std::memcpy(&out, buf.data() + offset, sizeof(T));
    return true;
}

bool read_cstring(const std::vector<uint8_t>& buf, size_t offset, std::string& out, size_t max_len = 256) {
    if (offset >= buf.size()) return false;
    out.clear();
    const size_t limit = std::min(buf.size(), offset + max_len);
    for (size_t i = offset; i < limit; ++i) {
        if (buf[i] == 0) return true;
        out.push_back(static_cast<char>(buf[i]));
    }
    return false;
}

bool rva_to_raw(uint32_t rva, const std::vector<PeSectionInfo>& sections, size_t& off_out) {
    for (const auto& s : sections) {
        const uint32_t span = std::max(s.virtual_size, s.raw_size);
        if (span == 0) continue;
        if (rva >= s.virtual_address && rva < s.virtual_address + span) {
            const uint32_t delta = rva - s.virtual_address;
            if (delta < s.raw_size) {
                off_out = static_cast<size_t>(s.raw_offset) + static_cast<size_t>(delta);
                return true;
            }
        }
    }
    return false;
}

bool get_data_directory(const std::vector<uint8_t>& raw, size_t dir_index,
                        uint32_t& rva_out, uint32_t& size_out, bool& is_pe32_plus_out) {
    uint32_t pe_off = 0;
    if (!read_le(raw, 0x3C, pe_off)) return false;
    uint16_t opt_size = 0;
    if (!read_le(raw, static_cast<size_t>(pe_off) + 20, opt_size)) return false;
    const size_t opt_off = static_cast<size_t>(pe_off) + 24;
    uint16_t opt_magic = 0;
    if (!read_le(raw, opt_off, opt_magic)) return false;
    is_pe32_plus_out = (opt_magic == 0x020b);

    const size_t num_rva_off = opt_off + (is_pe32_plus_out ? 108 : 92);
    const size_t dir_table_off = opt_off + (is_pe32_plus_out ? 112 : 96);
    uint32_t num_dirs = 0;
    if (!read_le(raw, num_rva_off, num_dirs) || dir_index >= std::min<uint32_t>(num_dirs, 16)) {
        return false;
    }
    const size_t off = dir_table_off + dir_index * 8;
    if (off + 8 > opt_off + opt_size) return false;
    return read_le(raw, off, rva_out) && read_le(raw, off + 4, size_out) && rva_out != 0 && size_out != 0;
}

#if defined(_WIN32)
class XboxUwpMemoryAllocator final : public IVirtualMemoryAllocator {
public:
    explicit XboxUwpMemoryAllocator(uint64_t budget_bytes) : budget_bytes_(budget_bytes) {}

    uint8_t* allocate_image(size_t size_bytes, std::string* error_out) override {
        if (size_bytes == 0 || size_bytes > budget_bytes_) {
            if (error_out) *error_out = "PE image exceeds Xbox Dev Mode memory budget.";
            return nullptr;
        }
#if defined(XWINE_PLATFORM_XBOX) || defined(WINAPI_FAMILY)
        // Documented UWP codeGeneration W^X path: allocate PAGE_READWRITE via VirtualAllocFromApp
        void* ptr = VirtualAllocFromApp(nullptr, size_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        void* ptr = VirtualAlloc(nullptr, size_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#endif
        if (!ptr) {
            if (error_out) *error_out = "VirtualAllocFromApp failed (ensure codeGeneration capability is declared).";
            return nullptr;
        }
        return static_cast<uint8_t*>(ptr);
    }

    bool protect_range(uint8_t* address, size_t size_bytes, MemoryProtection prot,
                       std::string* error_out) override {
        if (prot == MemoryProtection::ExecuteReadWrite) {
            if (error_out) *error_out = "Blocked RWX under strict Xbox UWP W^X policy.";
            return false;
        }
        ULONG win_prot = PAGE_READONLY;
        switch (prot) {
            case MemoryProtection::NoAccess:    win_prot = PAGE_NOACCESS; break;
            case MemoryProtection::ReadOnly:    win_prot = PAGE_READONLY; break;
            case MemoryProtection::ReadWrite:   win_prot = PAGE_READWRITE; break;
            case MemoryProtection::ExecuteRead: win_prot = PAGE_EXECUTE_READ; break;
            case MemoryProtection::ExecuteReadWrite: return false;
        }
        ULONG old_prot = 0;
#if defined(XWINE_PLATFORM_XBOX) || defined(WINAPI_FAMILY)
        const BOOL ok = VirtualProtectFromApp(address, size_bytes, win_prot, &old_prot);
#else
        const BOOL ok = VirtualProtect(address, size_bytes, win_prot, &old_prot);
#endif
        if (!ok && error_out) {
            *error_out = "VirtualProtectFromApp failed transitioning page permissions.";
        }
        return ok != 0;
    }

    void release_image(uint8_t* base, size_t /*size_bytes*/) override {
        if (base) VirtualFree(base, 0, MEM_RELEASE);
    }

    bool supports_native_x64_execution() const override {
#if defined(_M_X64) || defined(__x86_64__)
        return true;
#else
        return false;
#endif
    }

private:
    uint64_t budget_bytes_;
};
#endif

} // namespace

// =============================================================================
// DryRunMemoryAllocator
// =============================================================================
DryRunMemoryAllocator::DryRunMemoryAllocator(uint64_t budget_bytes) : budget_bytes_(budget_bytes) {}

uint8_t* DryRunMemoryAllocator::allocate_image(size_t size_bytes, std::string* error_out) {
    if (size_bytes == 0 || size_bytes > budget_bytes_) {
        if (error_out) *error_out = "Requested PE image size exceeds configured Xbox memory cap.";
        return nullptr;
    }
    auto block = std::make_unique<uint8_t[]>(size_bytes);
    std::memset(block.get(), 0, size_bytes);
    uint8_t* raw = block.get();
    owned_blocks_.push_back(std::move(block));
    return raw;
}

bool DryRunMemoryAllocator::protect_range(uint8_t* address, size_t /*size_bytes*/,
                                          MemoryProtection prot, std::string* error_out) {
    if (prot == MemoryProtection::ExecuteReadWrite) {
        if (error_out) *error_out = "RWX protection rejected under strict UWP W^X policy.";
        return false;
    }
    protection_log_.push_back({reinterpret_cast<uintptr_t>(address), prot});
    return true;
}

void DryRunMemoryAllocator::release_image(uint8_t* base, size_t /*size_bytes*/) {
    owned_blocks_.erase(
        std::remove_if(owned_blocks_.begin(), owned_blocks_.end(),
                       [base](const std::unique_ptr<uint8_t[]>& b) { return b.get() == base; }),
        owned_blocks_.end());
}

std::unique_ptr<IVirtualMemoryAllocator> create_platform_pe_allocator(uint64_t budget_bytes) {
#if defined(_WIN32)
    return std::make_unique<XboxUwpMemoryAllocator>(budget_bytes);
#else
    return std::make_unique<DryRunMemoryAllocator>(budget_bytes);
#endif
}

// =============================================================================
// PeImageMapper
// =============================================================================
PeExtendedDirectories PeImageMapper::parse_extended_directories(const std::vector<uint8_t>& raw_pe,
                                                                const PeInspectionReport& report) {
    PeExtendedDirectories dirs;
    if (!report.valid_pe) return dirs;

    bool is_64 = false;
    uint32_t rva = 0, sz = 0;

    // 1. Export Directory (index 0)
    if (get_data_directory(raw_pe, kDirExport, rva, sz, is_64)) {
        size_t exp_off = 0;
        if (rva_to_raw(rva, report.sections, exp_off)) {
            uint32_t name_rva = 0, ord_base = 0, num_funcs = 0, num_names = 0;
            uint32_t funcs_rva = 0, names_rva = 0, ords_rva = 0;
            if (read_le(raw_pe, exp_off + 12, name_rva) &&
                read_le(raw_pe, exp_off + 16, ord_base) &&
                read_le(raw_pe, exp_off + 20, num_funcs) &&
                read_le(raw_pe, exp_off + 24, num_names) &&
                read_le(raw_pe, exp_off + 28, funcs_rva) &&
                read_le(raw_pe, exp_off + 32, names_rva) &&
                read_le(raw_pe, exp_off + 36, ords_rva)) {
                size_t dll_off = 0;
                if (rva_to_raw(name_rva, report.sections, dll_off)) {
                    read_cstring(raw_pe, dll_off, dirs.export_dll_name);
                }
                size_t names_off = 0, ords_off = 0, funcs_off = 0;
                if (rva_to_raw(names_rva, report.sections, names_off) &&
                    rva_to_raw(ords_rva, report.sections, ords_off) &&
                    rva_to_raw(funcs_rva, report.sections, funcs_off)) {
                    const uint32_t limit = std::min<uint32_t>(num_names, 4096);
                    for (uint32_t i = 0; i < limit; ++i) {
                        uint32_t sym_name_rva = 0;
                        uint16_t ord_idx = 0;
                        if (!read_le(raw_pe, names_off + i * 4, sym_name_rva) ||
                            !read_le(raw_pe, ords_off + i * 2, ord_idx)) {
                            break;
                        }
                        if (ord_idx >= num_funcs) continue;
                        uint32_t fn_rva = 0;
                        size_t sym_off = 0;
                        std::string sym_name;
                        if (read_le(raw_pe, funcs_off + static_cast<size_t>(ord_idx) * 4, fn_rva) &&
                            rva_to_raw(sym_name_rva, report.sections, sym_off) &&
                            read_cstring(raw_pe, sym_off, sym_name)) {
                            dirs.exports.push_back({std::move(sym_name), ord_base + ord_idx, fn_rva});
                        }
                    }
                }
            }
        }
    }

    // 2. Base Relocation Directory (index 5)
    if (get_data_directory(raw_pe, kDirBaseReloc, rva, sz, is_64)) {
        size_t rel_off = 0;
        if (rva_to_raw(rva, report.sections, rel_off)) {
            size_t cursor = 0;
            while (cursor + 8 <= sz && rel_off + cursor + 8 <= raw_pe.size()) {
                uint32_t page_rva = 0, block_sz = 0;
                if (!read_le(raw_pe, rel_off + cursor, page_rva) ||
                    !read_le(raw_pe, rel_off + cursor + 4, block_sz) ||
                    block_sz < 8 || cursor + block_sz > sz) {
                    break;
                }
                const size_t entry_count = (block_sz - 8) / 2;
                for (size_t e = 0; e < entry_count; ++e) {
                    uint16_t word = 0;
                    if (!read_le(raw_pe, rel_off + cursor + 8 + e * 2, word)) break;
                    const uint8_t type = static_cast<uint8_t>((word >> 12) & 0xF);
                    const uint16_t off = word & 0x0FFF;
                    if (type != 0) { // Skip IMAGE_REL_BASED_ABSOLUTE padding
                        dirs.relocations.push_back({page_rva + off, type});
                    }
                }
                cursor += block_sz;
            }
        }
    }

    // 3. Delay-Load Import Directory (index 13)
    if (get_data_directory(raw_pe, kDirDelayImport, rva, sz, is_64)) {
        size_t dly_off = 0;
        if (rva_to_raw(rva, report.sections, dly_off)) {
            for (size_t i = 0; i < 256; ++i) {
                const size_t desc = dly_off + i * 32;
                uint32_t attrs = 0, dll_name_rva = 0;
                if (!read_le(raw_pe, desc + 0, attrs) || !read_le(raw_pe, desc + 4, dll_name_rva)) break;
                if (dll_name_rva == 0) break;
                size_t str_off = 0;
                std::string name;
                if (rva_to_raw(dll_name_rva, report.sections, str_off) && read_cstring(raw_pe, str_off, name)) {
                    dirs.delay_load_dlls.push_back(std::move(name));
                }
            }
        }
    }

    // 4. TLS Directory (index 9)
    if (get_data_directory(raw_pe, kDirTls, rva, sz, is_64)) {
        size_t tls_off = 0;
        if (rva_to_raw(rva, report.sections, tls_off)) {
            uint64_t cb_array_va = 0;
            if (is_64) {
                read_le(raw_pe, tls_off + 24, cb_array_va);
            } else {
                uint32_t va32 = 0;
                if (read_le(raw_pe, tls_off + 12, va32)) cb_array_va = va32;
            }
            if (cb_array_va > report.image_base) {
                const uint32_t cb_rva = static_cast<uint32_t>(cb_array_va - report.image_base);
                size_t cb_off = 0;
                if (rva_to_raw(cb_rva, report.sections, cb_off)) {
                    for (size_t c = 0; c < 32; ++c) {
                        uint64_t cb = 0;
                        if (is_64) {
                            if (!read_le(raw_pe, cb_off + c * 8, cb) || cb == 0) break;
                        } else {
                            uint32_t cb32 = 0;
                            if (!read_le(raw_pe, cb_off + c * 4, cb32) || cb32 == 0) break;
                            cb = cb32;
                        }
                        dirs.tls_callback_vas.push_back(cb);
                    }
                }
            }
        }
    }

    return dirs;
}

MappedPeImage PeImageMapper::map_pe_bytes(const std::vector<uint8_t>& raw_pe,
                                          IVirtualMemoryAllocator& allocator,
                                          Win32ShimEnvironment& shims,
                                          const std::string& label) {
    MappedPeImage result;
    const auto report = PeInspector::inspect_bytes(raw_pe, label);
    if (!report.valid_pe) {
        result.error_message = "PE validation failed: " + report.error_message;
        return result;
    }
    if (report.format != PeFormat::PE32Plus || report.architecture != PeArchitecture::X86_64) {
        result.error_message = "PeImageMapper requires 64-bit x86-64 PE32+ image (got " +
                               to_string(report.architecture) + ").";
        return result;
    }
    if (report.size_of_image < 0x1000 || report.size_of_image > (1024U * 1024U * 1024U)) {
        result.error_message = "PE SizeOfImage out of valid bounds.";
        return result;
    }

    result.extended_dirs = parse_extended_directories(raw_pe, report);

    std::string alloc_err;
    uint8_t* base = allocator.allocate_image(report.size_of_image, &alloc_err);
    if (!base) {
        result.error_message = alloc_err;
        return result;
    }
    result.base_address = base;
    result.image_size = report.size_of_image;
    result.entry_point_rva = report.entry_point_rva;

    // 1. Copy PE headers (up to first section raw offset, bounded by SizeOfImage)
    uint32_t headers_copy = std::min<uint32_t>(0x400, static_cast<uint32_t>(raw_pe.size()));
    for (const auto& sec : report.sections) {
        if (sec.raw_offset > 0) {
            headers_copy = std::min(headers_copy, sec.raw_offset);
        }
    }
    headers_copy = std::min<uint32_t>(headers_copy, report.size_of_image);
    std::memcpy(base, raw_pe.data(), headers_copy);

    // 2. Map each section into its virtual address
    for (const auto& sec : report.sections) {
        if (sec.virtual_address >= report.size_of_image) continue;
        const size_t max_copy = std::min<size_t>(sec.raw_size, report.size_of_image - sec.virtual_address);
        if (max_copy > 0 && static_cast<size_t>(sec.raw_offset) + max_copy <= raw_pe.size()) {
            std::memcpy(base + sec.virtual_address, raw_pe.data() + sec.raw_offset, max_copy);
        }
    }

    // 3. Apply Base Relocations relative to mapped_base - preferred_image_base
    const int64_t delta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(base)) -
                          static_cast<int64_t>(report.image_base);
    result.relocation_delta = delta;
    if (delta != 0) {
        for (const auto& rel : result.extended_dirs.relocations) {
            if (rel.type == 10 && static_cast<size_t>(rel.rva) + 8 <= result.image_size) { // IMAGE_REL_BASED_DIR64
                uint64_t val = 0;
                std::memcpy(&val, base + rel.rva, 8);
                val = static_cast<uint64_t>(static_cast<int64_t>(val) + delta);
                std::memcpy(base + rel.rva, &val, 8);
                ++result.relocations_applied;
            } else if (rel.type == 3 && static_cast<size_t>(rel.rva) + 4 <= result.image_size) { // HIGHLOW
                uint32_t val = 0;
                std::memcpy(&val, base + rel.rva, 4);
                val = static_cast<uint32_t>(static_cast<int64_t>(val) + delta);
                std::memcpy(base + rel.rva, &val, 4);
                ++result.relocations_applied;
            }
        }
    }

    // 4. Patch Import Address Table (IAT) entries against Win32ShimEnvironment
    bool is_64 = true;
    uint32_t imp_rva = 0, imp_sz = 0;
    if (get_data_directory(raw_pe, kDirImport, imp_rva, imp_sz, is_64)) {
        for (size_t idx = 0; idx < 512; ++idx) {
            const size_t desc_rva = static_cast<size_t>(imp_rva) + idx * 20;
            if (desc_rva + 20 > result.image_size) break;
            uint32_t orig_thunk_rva = 0, name_rva = 0, iat_rva = 0;
            std::memcpy(&orig_thunk_rva, base + desc_rva + 0, 4);
            std::memcpy(&name_rva, base + desc_rva + 12, 4);
            std::memcpy(&iat_rva, base + desc_rva + 16, 4);
            if (orig_thunk_rva == 0 && name_rva == 0 && iat_rva == 0) break;
            if (name_rva >= result.image_size || iat_rva >= result.image_size) continue;

            std::string dll_name(reinterpret_cast<const char*>(base + name_rva));
            const uint32_t lookup_rva = (orig_thunk_rva != 0) ? orig_thunk_rva : iat_rva;
            for (size_t f = 0; f < 2048; ++f) {
                const size_t l_off = static_cast<size_t>(lookup_rva) + f * 8;
                const size_t i_off = static_cast<size_t>(iat_rva) + f * 8;
                if (l_off + 8 > result.image_size || i_off + 8 > result.image_size) break;
                uint64_t entry = 0;
                std::memcpy(&entry, base + l_off, 8);
                if (entry == 0) break;

                std::string sym_name;
                if ((entry & (1ULL << 63)) != 0) {
                    sym_name = "#" + std::to_string(entry & 0xFFFFULL);
                } else {
                    const size_t hint_rva = static_cast<size_t>(entry & 0x7FFFFFFFULL);
                    if (hint_rva + 2 < result.image_size) {
                        sym_name = std::string(reinterpret_cast<const char*>(base + hint_rva + 2));
                    }
                }

                auto resolved = shims.resolve_import(dll_name, sym_name);
                if (resolved.resolved) {
                    uint64_t addr64 = static_cast<uint64_t>(resolved.address);
                    std::memcpy(base + i_off, &addr64, 8);
                    ++result.imports_resolved;
                    result.resolved_shims.push_back(resolved.shim_name);
                } else {
                    ++result.imports_unresolved;
                    result.unresolved_symbols.push_back(dll_name + "!" + sym_name);
                }
            }
        }
    }

    // 5. Enforce Strict W^X Memory Protection per Section (VirtualProtectFromApp RW -> RX / RO / RW)
    for (const auto& sec : report.sections) {
        if (sec.virtual_address >= result.image_size) continue;
        const size_t sec_sz = std::min<size_t>(
            std::max(sec.virtual_size, sec.raw_size),
            result.image_size - sec.virtual_address);
        if (sec_sz == 0) continue;

        MemoryProtection target_prot = MemoryProtection::ReadOnly;
        if (sec.executable && sec.writable) {
            // Reject RWX under strict Xbox UWP W^X policy
            result.error_message = "Section '" + sec.name + "' demands RWX, violating Xbox UWP W^X policy.";
            allocator.release_image(base, result.image_size);
            result.base_address = nullptr;
            return result;
        } else if (sec.executable) {
            target_prot = MemoryProtection::ExecuteRead;
        } else if (sec.writable) {
            target_prot = MemoryProtection::ReadWrite;
        }

        std::string prot_err;
        if (!allocator.protect_range(base + sec.virtual_address, sec_sz, target_prot, &prot_err)) {
            result.error_message = prot_err;
            allocator.release_image(base, result.image_size);
            result.base_address = nullptr;
            return result;
        }
        ++result.wx_transitions_applied;
    }

    result.mapped = true;
    result.ready_for_native_x64_execution =
        allocator.supports_native_x64_execution() && (result.imports_unresolved == 0);
    return result;
}

MappedPeImage PeImageMapper::map_pe_file(const std::filesystem::path& path,
                                         IVirtualMemoryAllocator& allocator,
                                         Win32ShimEnvironment& shims) {
    MappedPeImage err;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        err.error_message = "Unable to open PE file: " + path.string();
        return err;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return map_pe_bytes(bytes, allocator, shims, path.string());
}

PeExecutionResult PeImageMapper::execute_mapped_x64(const MappedPeImage& image,
                                                    const IVirtualMemoryAllocator& allocator,
                                                    Win32ShimEnvironment& shims) {
    PeExecutionResult res;
    if (!image.mapped || !image.base_address) {
        res.status_message = "Image is not mapped.";
        return res;
    }
    if (!allocator.supports_native_x64_execution()) {
        res.status_message =
            "Mapped, relocated (" + std::to_string(image.relocations_applied) + " fixups), and IAT-bound (" +
            std::to_string(image.imports_resolved) + " shims) in non-executing dry-run mode. "
            "Native x86-64 execution is only permitted on Windows/Xbox Series S x64 targets.";
        return res;
    }
    if (image.imports_unresolved > 0) {
        res.status_message = "Execution aborted: " + std::to_string(image.imports_unresolved) +
                             " unresolved Win32 import(s) would fault if invoked.";
        return res;
    }

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    using EntryFn = uint32_t (*)();
    std::jmp_buf exit_jmp;
    shims.set_exit_jump_buffer(&exit_jmp);
    Win32ShimEnvironment::set_active_instance(&shims);
    auto entry = reinterpret_cast<EntryFn>(image.base_address + image.entry_point_rva);
    platform::Logger::instance().info("PeLoader", "Invoking mapped x86-64 PE32+ entry point (Phase 7 experimental)");
    uint32_t ret = 0;
    if (setjmp(exit_jmp) == 0) {
        ret = entry();
    }
    shims.set_exit_jump_buffer(nullptr);
    Win32ShimEnvironment::set_active_instance(nullptr);
    res.executed = true;
    res.exit_code = shims.exit_requested() ? static_cast<int32_t>(shims.exit_code()) : static_cast<int32_t>(ret);
    res.status_message = "Executed x86-64 PE32+ entry point natively via VirtualAllocFromApp/VirtualProtectFromApp.";
    return res;
#else
    (void)shims;
    res.status_message = "Native x86-64 invocation is compiled out on non-Windows/ARM64 hosts.";
    return res;
#endif
}

} // namespace xwine::runtime
