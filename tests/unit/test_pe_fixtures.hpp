#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace xwine::test {

// Synthesizes a minimal, structurally valid PE32 or PE32+ image with real
// .text and .idata sections and genuine IMAGE_IMPORT_DESCRIPTOR + thunk entries
// so PeInspector can be rigorously verified without shipping third-party binaries.
inline std::vector<uint8_t> build_synthetic_pe(
    bool is_64bit,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& imports,
    bool rwx_text_section = false,
    bool clr_header = false) {
    // Layout:
    // 0x0000 - 0x01FF : Headers (DOS + PE + COFF + Optional + 2 Section Headers)
    // 0x0200 - 0x03FF : .text  (RVA 0x1000, Raw 0x0200, Size 0x0200)
    // 0x0400 - 0x0FFF : .idata (RVA 0x2000, Raw 0x0400, Size 0x0C00)
    std::vector<uint8_t> buf(0x1000, 0);

    auto write_u16 = [&](size_t off, uint16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    auto write_u32 = [&](size_t off, uint32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto write_u64 = [&](size_t off, uint64_t v) { std::memcpy(buf.data() + off, &v, 8); };

    // DOS Header
    write_u16(0x00, 0x5A4D); // "MZ"
    constexpr uint32_t kPeOff = 0x80;
    write_u32(0x3C, kPeOff);

    // PE Signature
    write_u32(kPeOff, 0x00004550); // "PE\0\0"

    // COFF File Header
    const size_t coff = kPeOff + 4;
    const uint16_t opt_size = is_64bit ? 240 : 224;
    write_u16(coff + 0, is_64bit ? 0x8664 : 0x014c); // AMD64 vs I386
    write_u16(coff + 2, 2);                          // 2 sections (.text, .idata)
    write_u32(coff + 4, 0x67000000);                 // Timestamp
    write_u16(coff + 16, opt_size);
    write_u16(coff + 18, 0x0022);                    // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    // Optional Header
    const size_t opt = coff + 20;
    write_u16(opt + 0, is_64bit ? 0x020b : 0x010b);
    write_u32(opt + 16, 0x1000); // AddressOfEntryPoint
    if (is_64bit) {
        write_u64(opt + 24, 0x0000000140000000ULL);
    } else {
        write_u32(opt + 28, 0x00400000U);
    }
    write_u32(opt + 56, 0x3000); // SizeOfImage
    write_u16(opt + 68, 2);      // IMAGE_SUBSYSTEM_WINDOWS_GUI
    write_u16(opt + 70, 0x0160); // HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT

    const size_t num_rva_off = opt + (is_64bit ? 108 : 92);
    const size_t dir_off = opt + (is_64bit ? 112 : 96);
    write_u32(num_rva_off, 16);

    // Import Directory (index 1) -> RVA 0x2000
    if (!imports.empty()) {
        write_u32(dir_off + 1 * 8 + 0, 0x2000);
        write_u32(dir_off + 1 * 8 + 4, static_cast<uint32_t>((imports.size() + 1) * 20));
    }
    if (clr_header) {
        write_u32(dir_off + 14 * 8 + 0, 0x1080);
        write_u32(dir_off + 14 * 8 + 4, 72);
    }

    // Section Headers
    const size_t sec0 = opt + opt_size;
    std::memcpy(buf.data() + sec0, ".text\0\0\0", 8);
    write_u32(sec0 + 8, 0x0200);
    write_u32(sec0 + 12, 0x1000);
    write_u32(sec0 + 16, 0x0200);
    write_u32(sec0 + 20, 0x0200);
    write_u32(sec0 + 36, rwx_text_section ? 0xE0000020U : 0x60000020U);

    const size_t sec1 = sec0 + 40;
    std::memcpy(buf.data() + sec1, ".idata\0\0", 8);
    write_u32(sec1 + 8, 0x0C00);
    write_u32(sec1 + 12, 0x2000);
    write_u32(sec1 + 16, 0x0C00);
    write_u32(sec1 + 20, 0x0400);
    write_u32(sec1 + 36, 0x40000040U);

    // Put recognizable instructions (ret) in .text
    buf[0x0200] = 0xC3;

    // Build .idata at file offset 0x0400 (RVA 0x2000)
    if (!imports.empty()) {
        const size_t idata_file = 0x0400;
        const uint32_t idata_rva = 0x2000;
        size_t cursor = (imports.size() + 1) * 20; // After null-terminated descriptor array
        const size_t entry_sz = is_64bit ? 8 : 4;

        for (size_t m = 0; m < imports.size(); ++m) {
            const auto& [dll_name, funcs] = imports[m];
            const uint32_t dll_rva = idata_rva + static_cast<uint32_t>(cursor);
            std::memcpy(buf.data() + idata_file + cursor, dll_name.c_str(), dll_name.size() + 1);
            cursor += dll_name.size() + 1;
            cursor = (cursor + 7) & ~size_t(7);

            const uint32_t thunk_table_rva = idata_rva + static_cast<uint32_t>(cursor);
            const size_t thunk_table_file = idata_file + cursor;
            cursor += (funcs.size() + 1) * entry_sz;

            for (size_t f = 0; f < funcs.size(); ++f) {
                const uint32_t hint_name_rva = idata_rva + static_cast<uint32_t>(cursor);
                write_u16(idata_file + cursor, static_cast<uint16_t>(f));
                std::memcpy(buf.data() + idata_file + cursor + 2, funcs[f].c_str(), funcs[f].size() + 1);
                cursor += 2 + funcs[f].size() + 1;
                cursor = (cursor + 1) & ~size_t(1);

                if (is_64bit) {
                    write_u64(thunk_table_file + f * 8, static_cast<uint64_t>(hint_name_rva));
                } else {
                    write_u32(thunk_table_file + f * 4, hint_name_rva);
                }
            }

            const size_t desc_file = idata_file + m * 20;
            write_u32(desc_file + 0, thunk_table_rva);  // OriginalFirstThunk
            write_u32(desc_file + 12, dll_rva);         // Name
            write_u32(desc_file + 16, thunk_table_rva); // FirstThunk
        }
    }

    return buf;
}

} // namespace xwine::test
