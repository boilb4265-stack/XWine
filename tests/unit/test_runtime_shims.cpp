#include "test_framework.hpp"
#include "test_pe_fixtures.hpp"

#include "xwine/input/controller_mapping.hpp"
#include "xwine/platform/xbox_capabilities.hpp"
#include "xwine/runtime/path_virtualizer.hpp"
#include "xwine/runtime/pe_loader.hpp"
#include "xwine/runtime/runtime_manager.hpp"
#include "xwine/runtime/virtual_registry.hpp"
#include "xwine/runtime/win32_shims.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path make_temp_dir(const std::string& tag) {
    auto p = std::filesystem::temp_directory_path() / ("xwine_phase34_" + tag);
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

// Builds a PE32+ image with a real .reloc section containing a DIR64 fixup pointing into .text
std::vector<uint8_t> build_relocatable_pe64(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& imports,
    bool rwx_text = false) {
    auto buf = xwine::test::build_synthetic_pe(true, imports, rwx_text, false);
    // Append a 3rd section (.reloc) at file offset 0x1000, RVA 0x3000, Size 0x200
    buf.resize(0x1200, 0);

    auto w16 = [&](size_t off, uint16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    auto w32 = [&](size_t off, uint32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto w64 = [&](size_t off, uint64_t v) { std::memcpy(buf.data() + off, &v, 8); };

    // Update NumberOfSections = 3 (coff + 2), SizeOfImage = 0x4000 (opt + 56)
    constexpr size_t kCoff = 0x84;
    constexpr size_t kOpt  = 0x98;
    w16(kCoff + 2, 3);
    w32(kOpt + 56, 0x4000);

    // Put a 64-bit pointer literal inside .text at RVA 0x1010 (file offset 0x0210):
    // Preferred ImageBase = 0x140000000 -> pointer to 0x140001050
    w64(0x0210, 0x0000000140001050ULL);

    // Base Relocation Directory (index 5) -> RVA 0x3000, Size 12 bytes
    const size_t dir_off = kOpt + 112;
    w32(dir_off + 5 * 8 + 0, 0x3000);
    w32(dir_off + 5 * 8 + 4, 12);

    // 3rd section header (.reloc) after 2nd section header (opt + 240 + 80)
    const size_t sec2 = kOpt + 240 + 80;
    std::memcpy(buf.data() + sec2, ".reloc\0\0", 8);
    w32(sec2 + 8,  0x0200);
    w32(sec2 + 12, 0x3000);
    w32(sec2 + 16, 0x0200);
    w32(sec2 + 20, 0x1000);
    w32(sec2 + 36, 0x42000040U); // READ | DISCARDABLE

    // IMAGE_BASE_RELOCATION block at file 0x1000: PageRVA=0x1000, BlockSize=12
    w32(0x1000, 0x1000);
    w32(0x1004, 12);
    // Entry 0: Type 10 (IMAGE_REL_BASED_DIR64) << 12 | Offset 0x010
    w16(0x1008, static_cast<uint16_t>((10U << 12) | 0x010U));
    // Entry 1: Type 0 (ABSOLUTE padding)
    w16(0x100A, 0);

    return buf;
}

} // namespace

XWINE_TEST(VirtualRegistryPersistsAllValueTypesAndNormalizesKeys) {
    using namespace xwine::runtime;
    auto dir = make_temp_dir("vreg");
    VirtualRegistry reg;

    XWINE_EXPECT_TRUE(reg.set_value("HKEY_CURRENT_USER/Software/XWine/Test", "Label",
                                    RegValue::make_string("Phase3Verified")));
    XWINE_EXPECT_TRUE(reg.set_value("hkcu\\software\\xwine\\test", "Count", RegValue::make_dword(77)));
    XWINE_EXPECT_TRUE(reg.set_value("HKCU\\Software\\XWine\\Test", "BigVal",
                                    RegValue::make_qword(0x1122334455667788ULL)));
    XWINE_EXPECT_TRUE(reg.set_value("HKCU\\Software\\XWine\\Test", "Blob",
                                    RegValue::make_binary({0xDE, 0xAD, 0xBE, 0xEF})));

    const auto file = dir / "registry.json";
    XWINE_EXPECT_TRUE(reg.save_to_file(file));

    VirtualRegistry loaded;
    XWINE_EXPECT_TRUE(loaded.load_from_file(file));

    auto s = loaded.get_value("HKCU\\Software\\XWine\\Test", "label");
    XWINE_EXPECT_TRUE(s.has_value());
    XWINE_EXPECT_EQ(std::get<std::string>(s->data), std::string("Phase3Verified"));

    auto dw = loaded.get_value("HKCU\\Software\\XWine\\Test", "COUNT");
    XWINE_EXPECT_TRUE(dw.has_value());
    XWINE_EXPECT_EQ(std::get<uint32_t>(dw->data), 77U);

    auto qw = loaded.get_value("HKCU\\Software\\XWine\\Test", "BigVal");
    XWINE_EXPECT_TRUE(qw.has_value());
    XWINE_EXPECT_EQ(std::get<uint64_t>(qw->data), 0x1122334455667788ULL);

    auto bin = loaded.get_value("HKCU\\Software\\XWine\\Test", "Blob");
    XWINE_EXPECT_TRUE(bin.has_value());
    XWINE_EXPECT_EQ(std::get<std::vector<uint8_t>>(bin->data).size(), 4U);
}

XWINE_TEST(PathVirtualizerJailsWindowsPathsAndBlocksTraversal) {
    using namespace xwine::runtime;
    auto dir = make_temp_dir("vpath");
    PathVirtualizer pv(dir);
    XWINE_EXPECT_TRUE(pv.ensure_prefix_layout());

    auto ok = pv.resolve_windows_path("C:\\Users\\XWine\\AppData\\Local\\save.dat");
    XWINE_EXPECT_TRUE(ok.allowed);
    XWINE_EXPECT_EQ(ok.normalized_windows_path, std::string("C:\\Users\\XWine\\AppData\\Local\\save.dat"));

    auto ext = pv.resolve_windows_path("\\\\?\\C:\\Windows\\System32\\kernel32.dll");
    XWINE_EXPECT_TRUE(ext.allowed);

    // Security rejections
    XWINE_EXPECT_FALSE(pv.resolve_windows_path("C:\\..\\..\\etc\\passwd").allowed);
    XWINE_EXPECT_FALSE(pv.resolve_windows_path("\\\\.\\PhysicalDrive0").allowed);
    XWINE_EXPECT_FALSE(pv.resolve_windows_path("\\\\server\\share\\file.exe").allowed);
    XWINE_EXPECT_FALSE(pv.resolve_windows_path("D:\\outside\\game.exe").allowed);
}

XWINE_TEST(Win32ShimEnvironmentExercisesKernel32User32Advapi32AndXInput) {
    using namespace xwine::runtime;
    auto dir = make_temp_dir("shims");
    Win32ShimEnvironment env(dir);

    // 1. kernel32 File I/O inside sandboxed C:
    const uint64_t fh = env.create_file("C:\\Users\\XWine\\AppData\\Local\\hello.txt", true, true);
    XWINE_EXPECT_TRUE(fh != 0);
    const std::string payload = "XWine Phase 4 Shim File Test";
    uint32_t written = 0;
    XWINE_EXPECT_TRUE(env.write_file(fh, payload.data(), static_cast<uint32_t>(payload.size()), &written));
    XWINE_EXPECT_EQ(written, payload.size());
    XWINE_EXPECT_TRUE(env.close_handle(fh));

    const uint64_t rf = env.create_file("C:\\Users\\XWine\\AppData\\Local\\hello.txt", false, false);
    XWINE_EXPECT_TRUE(rf != 0);
    auto sz = env.get_file_size(rf);
    XWINE_EXPECT_TRUE(sz.has_value());
    XWINE_EXPECT_EQ(*sz, payload.size());
    std::string read_back(payload.size(), '\0');
    uint32_t nread = 0;
    XWINE_EXPECT_TRUE(env.read_file(rf, read_back.data(), static_cast<uint32_t>(read_back.size()), &nread));
    XWINE_EXPECT_EQ(read_back, payload);
    XWINE_EXPECT_TRUE(env.close_handle(rf));

    // Traversal must be blocked
    XWINE_EXPECT_EQ(env.create_file("C:\\..\\..\\escape.txt", true, true), 0ULL);

    // 2. Heap & Timing & Debug & ExitProcess trap
    void* mem = env.heap_alloc(128, true);
    XWINE_EXPECT_TRUE(mem != nullptr);
    XWINE_EXPECT_EQ(env.active_heap_allocations(), 1U);
    XWINE_EXPECT_TRUE(env.heap_free(mem));
    XWINE_EXPECT_EQ(env.active_heap_allocations(), 0U);
    XWINE_EXPECT_TRUE(env.query_performance_counter() > 0);
    env.output_debug_string("Shim probe log line");
    XWINE_EXPECT_EQ(env.debug_messages().size(), 1U);
    env.request_exit_process(42);
    XWINE_EXPECT_TRUE(env.exit_requested());
    XWINE_EXPECT_EQ(env.exit_code(), 42U);

    // 3. user32 virtual window & message pump
    XWINE_EXPECT_TRUE(env.register_window_class("XWineTestClass"));
    const uint64_t hwnd = env.create_window("XWineTestClass", "Test Title", 1920, 1080);
    XWINE_EXPECT_TRUE(hwnd != 0);
    XWINE_EXPECT_TRUE(env.show_window(hwnd, true));
    env.post_quit_message(0);
    VirtualWindowMessage msg{};
    XWINE_EXPECT_TRUE(env.peek_message(msg, true));
    XWINE_EXPECT_EQ(msg.message, kWmQuit);
    XWINE_EXPECT_TRUE(env.destroy_window(hwnd));

    // 4. advapi32 virtual registry
    const uint64_t hk = env.reg_create_or_open_key("HKCU\\Software\\XWine\\ShimTest", true);
    XWINE_EXPECT_TRUE(hk != 0);
    XWINE_EXPECT_TRUE(env.reg_set_dword(hk, "FpsCap", 60));
    auto qv = env.reg_query_value(hk, "FpsCap");
    XWINE_EXPECT_TRUE(qv.has_value());
    XWINE_EXPECT_EQ(std::get<uint32_t>(qv->data), 60U);
    XWINE_EXPECT_TRUE(env.reg_close_key(hk));

    // 5. xinput1_4 live snapshot
    xwine::input::XInputStateSnapshot snap{};
    snap.wButtons = xwine::input::kXInputA | xwine::input::kXInputRightShoulder;
    snap.bRightTrigger = 200;
    env.set_gamepad_snapshot(snap, true);
    xwine::input::XInputStateSnapshot out{};
    XWINE_EXPECT_TRUE(env.xinput_get_state(0, out));
    XWINE_EXPECT_EQ(out.wButtons, snap.wButtons);
    XWINE_EXPECT_EQ(out.bRightTrigger, 200);
}

XWINE_TEST(PeImageMapperAppliesDir64RelocationsBindsShimsAndEnforcesWx) {
    using namespace xwine::runtime;
    auto dir = make_temp_dir("peloader");
    Win32ShimEnvironment shims(dir);
    DryRunMemoryAllocator alloc(xwine::platform::kXboxDevModeGameRamCapBytes);

    auto pe_bytes = build_relocatable_pe64({
        {"KERNEL32.dll", {"OutputDebugStringA", "QueryPerformanceCounter", "UnimplementedWin32Symbol"}},
        {"USER32.dll", {"CreateWindowExW", "PeekMessageW"}},
        {"XINPUT1_4.dll", {"XInputGetState"}}
    });

    auto mapped = PeImageMapper::map_pe_bytes(pe_bytes, alloc, shims, "probe64.exe");
    XWINE_EXPECT_TRUE(mapped.mapped);
    XWINE_EXPECT_EQ(mapped.relocations_applied, 1U);
    XWINE_EXPECT_EQ(mapped.imports_resolved, 5U);
    XWINE_EXPECT_EQ(mapped.imports_unresolved, 1U);
    XWINE_EXPECT_EQ(shims.unsupported_telemetry().unique_missing_count(), 1U);
    XWINE_EXPECT_TRUE(mapped.wx_transitions_applied >= 2U);

    // Verify exact DIR64 relocation math at RVA 0x1010!
    // Original pointer was 0x140001050 (PreferredBase + 0x1050), so relocated MUST equal base_address + 0x1050!
    uint64_t relocated_ptr = 0;
    std::memcpy(&relocated_ptr, mapped.base_address + 0x1010, 8);
    const uint64_t expected_ptr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mapped.base_address) + 0x1050);
    XWINE_EXPECT_EQ(relocated_ptr, expected_ptr);

    // Dry-run execution on macOS MUST honestly report executed = false
    auto exec_res = PeImageMapper::execute_mapped_x64(mapped, alloc, shims);
    XWINE_EXPECT_FALSE(exec_res.executed);
    alloc.release_image(mapped.base_address, mapped.image_size);

    // Verify strict W^X rejection on RWX .text section
    auto rwx_pe = build_relocatable_pe64({{"KERNEL32.dll", {"ExitProcess"}}}, true);
    auto rwx_mapped = PeImageMapper::map_pe_bytes(rwx_pe, alloc, shims, "rwx_bad.exe");
    XWINE_EXPECT_FALSE(rwx_mapped.mapped);
}

int main() {
    return xwine::test::run_all_tests("XWine Phase 3/4 Runtime Loader & Win32 Shim Tests");
}
