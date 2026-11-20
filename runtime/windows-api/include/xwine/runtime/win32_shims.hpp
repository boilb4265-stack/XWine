#pragma once

#include "xwine/input/controller_mapping.hpp"
#include "xwine/runtime/path_virtualizer.hpp"
#include "xwine/runtime/virtual_registry.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace xwine::runtime {

struct UnsupportedApiCall {
    std::string dll_name;
    std::string symbol_name;
    uint32_t hit_count = 0;
};

class UnsupportedApiTelemetry {
public:
    void record_missing(const std::string& dll_name, const std::string& symbol_name);
    std::vector<UnsupportedApiCall> snapshot() const;
    size_t unique_missing_count() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::map<std::string, UnsupportedApiCall> entries_;
};

// Virtual User32 Window & Message types (portable, zero <windows.h> dependency on macOS)
struct VirtualRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 1920;
    int32_t bottom = 1080;
};

struct VirtualWindow {
    uint64_t hwnd = 0;
    std::string class_name;
    std::string title;
    VirtualRect client_rect{0, 0, 1920, 1080};
    bool visible = false;
    bool destroyed = false;
};

struct VirtualWindowMessage {
    uint64_t hwnd = 0;
    uint32_t message = 0;
    uint64_t wparam = 0;
    int64_t lparam = 0;
};

constexpr uint32_t kWmNull    = 0x0000;
constexpr uint32_t kWmDestroy = 0x0002;
constexpr uint32_t kWmSize    = 0x0005;
constexpr uint32_t kWmClose   = 0x0010;
constexpr uint32_t kWmQuit    = 0x0012;
constexpr uint32_t kWmKeyDown = 0x0100;
constexpr uint32_t kWmKeyUp   = 0x0101;

// Phase 4 Minimal Win32-to-OneCoreUAP In-Process Shim Environment.
// All shims are unit-testable on macOS AND callable via Microsoft x64 ABI thunks on Xbox/Windows x64.
class Win32ShimEnvironment {
public:
    explicit Win32ShimEnvironment(const std::filesystem::path& prefix_root);
    ~Win32ShimEnvironment();

    // Active process-wide context pointer used by x64 ABI C trampolines during Phase 7 execution
    static Win32ShimEnvironment* active_instance();
    static void set_active_instance(Win32ShimEnvironment* env);

    // Import resolver for PeImageMapper (maps DLL + symbol -> shim address, or records missing telemetry)
    struct ResolvedImport {
        bool resolved = false;
        uintptr_t address = 0;
        std::string shim_name;
    };
    ResolvedImport resolve_import(const std::string& dll_name, const std::string& symbol_name);

    // --- kernel32.dll minimal subset ---
    uint32_t get_last_error() const { return last_error_; }
    void set_last_error(uint32_t err) { last_error_ = err; }

    uint64_t query_performance_counter() const;
    uint64_t query_performance_frequency() const;
    uint64_t get_tick_count64() const;

    void output_debug_string(const std::string& msg);
    const std::vector<std::string>& debug_messages() const { return debug_messages_; }

    uint64_t create_file(const std::string& windows_path, bool write_access, bool create_always);
    bool write_file(uint64_t handle, const void* data, uint32_t bytes, uint32_t* written_out = nullptr);
    bool read_file(uint64_t handle, void* buffer, uint32_t bytes_to_read, uint32_t* read_out = nullptr);
    std::optional<uint64_t> get_file_size(uint64_t handle) const;
    bool close_handle(uint64_t handle);

    void* heap_alloc(size_t bytes, bool zero_init);
    bool heap_free(void* ptr);
    size_t active_heap_allocations() const { return heap_allocs_.size(); }

    void set_command_line(std::string cmd) { command_line_ = std::move(cmd); }
    const std::string& command_line() const { return command_line_; }
    void set_env(const std::string& name, const std::string& value);
    std::optional<std::string> get_env(const std::string& name) const;

    void request_exit_process(uint32_t code);
    bool exit_requested() const { return exit_requested_; }
    uint32_t exit_code() const { return exit_code_; }

    // --- user32.dll minimal in-process virtual window & message pump ---
    bool register_window_class(const std::string& class_name);
    uint64_t create_window(const std::string& class_name, const std::string& title, int32_t width, int32_t height);
    bool show_window(uint64_t hwnd, bool visible);
    bool destroy_window(uint64_t hwnd);
    std::optional<VirtualWindow> get_window(uint64_t hwnd) const;
    void post_message(const VirtualWindowMessage& msg);
    void post_quit_message(int32_t exit_code);
    bool peek_message(VirtualWindowMessage& out, bool remove = true);

    // --- advapi32.dll minimal virtual registry subset ---
    uint64_t reg_create_or_open_key(const std::string& full_path, bool create_if_missing);
    bool reg_set_string(uint64_t hkey, const std::string& name, const std::string& value);
    bool reg_set_dword(uint64_t hkey, const std::string& name, uint32_t value);
    std::optional<RegValue> reg_query_value(uint64_t hkey, const std::string& name) const;
    bool reg_close_key(uint64_t hkey);

    // --- xinput1_4.dll minimal subset ---
    void set_gamepad_snapshot(const input::XInputStateSnapshot& snapshot, bool connected);
    bool xinput_get_state(uint32_t user_index, input::XInputStateSnapshot& out) const;

    // Accessors
    PathVirtualizer& path_virtualizer() { return paths_; }
    VirtualRegistry& virtual_registry() { return registry_; }
    UnsupportedApiTelemetry& unsupported_telemetry() { return unsupported_; }
    const UnsupportedApiTelemetry& unsupported_telemetry() const { return unsupported_; }

    std::vector<std::string> list_implemented_shims() const;

private:
    struct OpenFileHandle {
        std::filesystem::path host_path;
        std::unique_ptr<std::fstream> stream;
        bool writable = false;
    };

    void register_builtin_shims();

    PathVirtualizer paths_;
    VirtualRegistry registry_;
    UnsupportedApiTelemetry unsupported_;

    mutable std::mutex mutex_;
    uint32_t last_error_ = 0;
    uint64_t next_handle_ = 0x100;
    uint64_t next_hwnd_ = 0x10000;
    uint64_t next_hkey_ = 0x20000;

    std::map<uint64_t, OpenFileHandle> file_handles_;
    std::map<void*, std::vector<uint8_t>> heap_allocs_;
    std::map<std::string, std::string> env_vars_;
    std::string command_line_ = "xwine-guest.exe";
    std::vector<std::string> debug_messages_;
    bool exit_requested_ = false;
    uint32_t exit_code_ = 0;

    std::vector<std::string> window_classes_;
    std::map<uint64_t, VirtualWindow> windows_;
    std::deque<VirtualWindowMessage> msg_queue_;
    std::map<uint64_t, std::string> open_reg_keys_;

    input::XInputStateSnapshot gamepad_snapshot_{};
    bool gamepad_connected_ = false;

    // lowercase "dll!symbol" -> shim function/token address
    std::map<std::string, uintptr_t> shim_table_;
};

} // namespace xwine::runtime
