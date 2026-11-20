#include "xwine/runtime/win32_shims.hpp"
#include "xwine/platform/logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

namespace xwine::runtime {

namespace {

std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string utf16_to_utf8_lossy(const uint16_t* w) {
    if (!w) return "";
    std::string out;
    for (size_t i = 0; w[i] != 0 && i < 4096; ++i) {
        if (w[i] < 0x80) out.push_back(static_cast<char>(w[i]));
        else out.push_back('?');
    }
    return out;
}

Win32ShimEnvironment* g_active_env = nullptr;

// =============================================================================
// Portable C ABI Trampolines (bound into Guest PE32+ IAT on Windows/Xbox x64,
// and assigned deterministic callable function pointers on macOS dry-run tests)
// =============================================================================
extern "C" {

uint32_t xwine_shim_GetLastError() {
    return g_active_env ? g_active_env->get_last_error() : 0;
}

void xwine_shim_SetLastError(uint32_t err) {
    if (g_active_env) g_active_env->set_last_error(err);
}

int32_t xwine_shim_QueryPerformanceCounter(int64_t* out_val) {
    if (!out_val) return 0;
    *out_val = static_cast<int64_t>(g_active_env ? g_active_env->query_performance_counter() : 0);
    return 1;
}

int32_t xwine_shim_QueryPerformanceFrequency(int64_t* out_val) {
    if (!out_val) return 0;
    *out_val = static_cast<int64_t>(g_active_env ? g_active_env->query_performance_frequency() : 1000000000ULL);
    return 1;
}

uint64_t xwine_shim_GetTickCount64() {
    return g_active_env ? g_active_env->get_tick_count64() : 0;
}

void xwine_shim_OutputDebugStringA(const char* str) {
    if (g_active_env && str) g_active_env->output_debug_string(str);
}

void xwine_shim_OutputDebugStringW(const uint16_t* wstr) {
    if (g_active_env && wstr) g_active_env->output_debug_string(utf16_to_utf8_lossy(wstr));
}

void xwine_shim_ExitProcess(uint32_t exit_code) {
    if (g_active_env) g_active_env->request_exit_process(exit_code);
}

uintptr_t xwine_shim_GetProcessHeap() {
    return static_cast<uintptr_t>(0x0000BEEF);
}

void* xwine_shim_HeapAlloc(uintptr_t /*heap*/, uint32_t flags, size_t bytes) {
    return g_active_env ? g_active_env->heap_alloc(bytes, (flags & 0x00000008U) != 0) : nullptr;
}

int32_t xwine_shim_HeapFree(uintptr_t /*heap*/, uint32_t /*flags*/, void* ptr) {
    return (g_active_env && g_active_env->heap_free(ptr)) ? 1 : 0;
}

const char* xwine_shim_GetCommandLineA() {
    return g_active_env ? g_active_env->command_line().c_str() : "";
}

uint64_t xwine_shim_CreateFileW(const uint16_t* path, uint32_t access, uint32_t /*share*/,
                                void* /*sec*/, uint32_t disp, uint32_t /*flags*/, uint64_t /*tmpl*/) {
    if (!g_active_env || !path) return ~uint64_t(0); // INVALID_HANDLE_VALUE
    const bool write_acc = (access & 0x40000000U) != 0;
    const bool create_always = (disp == 2 || disp == 4);
    const uint64_t h = g_active_env->create_file(utf16_to_utf8_lossy(path), write_acc, create_always);
    return h == 0 ? ~uint64_t(0) : h;
}

int32_t xwine_shim_WriteFile(uint64_t handle, const void* buf, uint32_t nbytes,
                             uint32_t* written, void* /*overlapped*/) {
    if (!g_active_env) return 0;
    return g_active_env->write_file(handle, buf, nbytes, written) ? 1 : 0;
}

int32_t xwine_shim_ReadFile(uint64_t handle, void* buf, uint32_t nbytes,
                            uint32_t* read_bytes, void* /*overlapped*/) {
    if (!g_active_env) return 0;
    return g_active_env->read_file(handle, buf, nbytes, read_bytes) ? 1 : 0;
}

int32_t xwine_shim_CloseHandle(uint64_t handle) {
    return (g_active_env && g_active_env->close_handle(handle)) ? 1 : 0;
}

uint64_t xwine_shim_CreateWindowExW(uint32_t /*ex*/, const uint16_t* cls, const uint16_t* title,
                                    uint32_t /*style*/, int32_t /*x*/, int32_t /*y*/,
                                    int32_t w, int32_t h, uint64_t /*parent*/, uint64_t /*menu*/,
                                    uintptr_t /*inst*/, void* /*param*/) {
    if (!g_active_env) return 0;
    return g_active_env->create_window(utf16_to_utf8_lossy(cls), utf16_to_utf8_lossy(title),
                                       w > 0 ? w : 1920, h > 0 ? h : 1080);
}

int32_t xwine_shim_ShowWindow(uint64_t hwnd, int32_t cmd) {
    return (g_active_env && g_active_env->show_window(hwnd, cmd != 0)) ? 1 : 0;
}

int32_t xwine_shim_DestroyWindow(uint64_t hwnd) {
    return (g_active_env && g_active_env->destroy_window(hwnd)) ? 1 : 0;
}

void xwine_shim_PostQuitMessage(int32_t code) {
    if (g_active_env) g_active_env->post_quit_message(code);
}

int32_t xwine_shim_PeekMessageW(VirtualWindowMessage* msg, uint64_t /*hwnd*/, uint32_t /*min*/,
                                uint32_t /*max*/, uint32_t remove_flag) {
    if (!g_active_env || !msg) return 0;
    return g_active_env->peek_message(*msg, (remove_flag & 1U) != 0) ? 1 : 0;
}

int32_t xwine_shim_RegCreateKeyExW(uint64_t root_key, const uint16_t* subkey, uint32_t /*res*/,
                                   uint16_t* /*cls*/, uint32_t /*opts*/, uint32_t /*sam*/,
                                   void* /*sec*/, uint64_t* out_hkey, uint32_t* /*disp*/) {
    if (!g_active_env || !subkey || !out_hkey) return 87; // ERROR_INVALID_PARAMETER
    std::string prefix = (root_key == 0x80000001ULL) ? "HKCU\\" : "HKLM\\";
    const uint64_t h = g_active_env->reg_create_or_open_key(prefix + utf16_to_utf8_lossy(subkey), true);
    if (h == 0) return 5; // ERROR_ACCESS_DENIED
    *out_hkey = h;
    return 0; // ERROR_SUCCESS
}

int32_t xwine_shim_RegCloseKey(uint64_t hkey) {
    return (g_active_env && g_active_env->reg_close_key(hkey)) ? 0 : 6;
}

uint32_t xwine_shim_XInputGetState(uint32_t user_index, void* state_out) {
    if (!g_active_env || !state_out) return 1167; // ERROR_DEVICE_NOT_CONNECTED
    struct RawXInputState {
        uint32_t dwPacketNumber;
        input::XInputStateSnapshot Gamepad;
    };
    auto* raw = static_cast<RawXInputState*>(state_out);
    if (!g_active_env->xinput_get_state(user_index, raw->Gamepad)) {
        return 1167; // ERROR_DEVICE_NOT_CONNECTED
    }
    static uint32_t packet = 1;
    raw->dwPacketNumber = packet++;
    return 0; // ERROR_SUCCESS
}

} // extern "C"

} // namespace

// =============================================================================
// UnsupportedApiTelemetry
// =============================================================================
void UnsupportedApiTelemetry::record_missing(const std::string& dll_name, const std::string& symbol_name) {
    const std::string key = lower_ascii(dll_name) + "!" + symbol_name;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = entries_[key];
    if (entry.hit_count == 0) {
        entry.dll_name = dll_name;
        entry.symbol_name = symbol_name;
    }
    ++entry.hit_count;
}

std::vector<UnsupportedApiCall> UnsupportedApiTelemetry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UnsupportedApiCall> out;
    out.reserve(entries_.size());
    for (const auto& [_, v] : entries_) out.push_back(v);
    return out;
}

size_t UnsupportedApiTelemetry::unique_missing_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void UnsupportedApiTelemetry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

// =============================================================================
// Win32ShimEnvironment
// =============================================================================
Win32ShimEnvironment::Win32ShimEnvironment(const std::filesystem::path& prefix_root)
    : paths_(prefix_root) {
    paths_.ensure_prefix_layout();
    env_vars_["OS"] = "Windows_NT";
    env_vars_["PROCESSOR_ARCHITECTURE"] = "AMD64";
    env_vars_["SYSTEMROOT"] = "C:\\Windows";
    env_vars_["USERPROFILE"] = "C:\\Users\\XWine";
    register_builtin_shims();
}

Win32ShimEnvironment::~Win32ShimEnvironment() {
    if (g_active_env == this) {
        g_active_env = nullptr;
    }
}

Win32ShimEnvironment* Win32ShimEnvironment::active_instance() {
    return g_active_env;
}

void Win32ShimEnvironment::set_active_instance(Win32ShimEnvironment* env) {
    g_active_env = env;
}

void Win32ShimEnvironment::register_builtin_shims() {
    auto add = [&](const std::string& dll, const std::string& sym, auto fn_ptr) {
        shim_table_[lower_ascii(dll) + "!" + sym] = reinterpret_cast<uintptr_t>(fn_ptr);
    };

    // kernel32.dll minimal OneCoreUAP-bridged subset
    add("kernel32.dll", "GetLastError",              &xwine_shim_GetLastError);
    add("kernel32.dll", "SetLastError",              &xwine_shim_SetLastError);
    add("kernel32.dll", "QueryPerformanceCounter",   &xwine_shim_QueryPerformanceCounter);
    add("kernel32.dll", "QueryPerformanceFrequency", &xwine_shim_QueryPerformanceFrequency);
    add("kernel32.dll", "GetTickCount64",            &xwine_shim_GetTickCount64);
    add("kernel32.dll", "OutputDebugStringA",        &xwine_shim_OutputDebugStringA);
    add("kernel32.dll", "OutputDebugStringW",        &xwine_shim_OutputDebugStringW);
    add("kernel32.dll", "ExitProcess",               &xwine_shim_ExitProcess);
    add("kernel32.dll", "GetProcessHeap",            &xwine_shim_GetProcessHeap);
    add("kernel32.dll", "HeapAlloc",                 &xwine_shim_HeapAlloc);
    add("kernel32.dll", "HeapFree",                  &xwine_shim_HeapFree);
    add("kernel32.dll", "GetCommandLineA",           &xwine_shim_GetCommandLineA);
    add("kernel32.dll", "CreateFileW",               &xwine_shim_CreateFileW);
    add("kernel32.dll", "WriteFile",                 &xwine_shim_WriteFile);
    add("kernel32.dll", "ReadFile",                  &xwine_shim_ReadFile);
    add("kernel32.dll", "CloseHandle",               &xwine_shim_CloseHandle);

    // user32.dll minimal in-process virtual window subset
    add("user32.dll", "CreateWindowExW",             &xwine_shim_CreateWindowExW);
    add("user32.dll", "ShowWindow",                  &xwine_shim_ShowWindow);
    add("user32.dll", "DestroyWindow",               &xwine_shim_DestroyWindow);
    add("user32.dll", "PostQuitMessage",             &xwine_shim_PostQuitMessage);
    add("user32.dll", "PeekMessageW",                &xwine_shim_PeekMessageW);

    // advapi32.dll virtual registry subset
    add("advapi32.dll", "RegCreateKeyExW",           &xwine_shim_RegCreateKeyExW);
    add("advapi32.dll", "RegCloseKey",               &xwine_shim_RegCloseKey);

    // xinput1_4.dll / xinput9_1_0.dll
    add("xinput1_4.dll", "XInputGetState",           &xwine_shim_XInputGetState);
    add("xinput9_1_0.dll", "XInputGetState",         &xwine_shim_XInputGetState);
}

Win32ShimEnvironment::ResolvedImport Win32ShimEnvironment::resolve_import(
    const std::string& dll_name, const std::string& symbol_name) {
    const std::string key = lower_ascii(dll_name) + "!" + symbol_name;
    auto it = shim_table_.find(key);
    if (it != shim_table_.end()) {
        return {true, it->second, key};
    }
    unsupported_.record_missing(dll_name, symbol_name);
    return {false, 0, ""};
}

std::vector<std::string> Win32ShimEnvironment::list_implemented_shims() const {
    std::vector<std::string> out;
    out.reserve(shim_table_.size());
    for (const auto& [k, _] : shim_table_) out.push_back(k);
    return out;
}

uint64_t Win32ShimEnvironment::query_performance_counter() const {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

uint64_t Win32ShimEnvironment::query_performance_frequency() const {
    using period = std::chrono::steady_clock::period;
    return static_cast<uint64_t>(period::den / period::num);
}

uint64_t Win32ShimEnvironment::get_tick_count64() const {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void Win32ShimEnvironment::output_debug_string(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        debug_messages_.push_back(msg);
    }
    platform::Logger::instance().info("GuestDebug", msg);
}

uint64_t Win32ShimEnvironment::create_file(const std::string& windows_path, bool write_access,
                                           bool create_always) {
    auto resolved = paths_.resolve_windows_path(windows_path);
    if (!resolved.allowed) {
        set_last_error(5); // ERROR_ACCESS_DENIED
        platform::Logger::instance().warn("Win32Shim", "CreateFile blocked: " + resolved.rejection_reason);
        return 0;
    }

    std::error_code ec;
    if (write_access && resolved.host_path.has_parent_path()) {
        std::filesystem::create_directories(resolved.host_path.parent_path(), ec);
    }

    auto stream = std::make_unique<std::fstream>();
    std::ios::openmode mode = std::ios::binary | std::ios::in;
    if (write_access) {
        mode |= std::ios::out;
        if (create_always || !std::filesystem::exists(resolved.host_path, ec)) {
            mode |= std::ios::trunc;
        }
    }
    stream->open(resolved.host_path, mode);
    if (!stream->is_open()) {
        set_last_error(2); // ERROR_FILE_NOT_FOUND
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t h = next_handle_++;
    file_handles_[h] = {resolved.host_path, std::move(stream), write_access};
    set_last_error(0);
    return h;
}

bool Win32ShimEnvironment::write_file(uint64_t handle, const void* data, uint32_t bytes,
                                      uint32_t* written_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = file_handles_.find(handle);
    if (it == file_handles_.end() || !it->second.writable || !it->second.stream) {
        set_last_error(6); // ERROR_INVALID_HANDLE
        return false;
    }
    it->second.stream->write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    it->second.stream->flush();
    if (!it->second.stream->good()) {
        set_last_error(29); // ERROR_WRITE_FAULT
        return false;
    }
    if (written_out) *written_out = bytes;
    return true;
}

bool Win32ShimEnvironment::read_file(uint64_t handle, void* buffer, uint32_t bytes_to_read,
                                     uint32_t* read_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = file_handles_.find(handle);
    if (it == file_handles_.end() || !it->second.stream) {
        set_last_error(6);
        return false;
    }
    it->second.stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes_to_read));
    const auto got = static_cast<uint32_t>(it->second.stream->gcount());
    if (read_out) *read_out = got;
    return true;
}

std::optional<uint64_t> Win32ShimEnvironment::get_file_size(uint64_t handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = file_handles_.find(handle);
    if (it == file_handles_.end()) return std::nullopt;
    std::error_code ec;
    const uint64_t sz = std::filesystem::file_size(it->second.host_path, ec);
    return ec ? std::nullopt : std::optional<uint64_t>(sz);
}

bool Win32ShimEnvironment::close_handle(uint64_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_handles_.erase(handle) > 0;
}

void* Win32ShimEnvironment::heap_alloc(size_t bytes, bool zero_init) {
    if (bytes == 0) bytes = 1;
    std::vector<uint8_t> block(bytes);
    if (zero_init) std::fill(block.begin(), block.end(), 0);
    void* ptr = block.data();
    std::lock_guard<std::mutex> lock(mutex_);
    heap_allocs_[ptr] = std::move(block);
    return ptr;
}

bool Win32ShimEnvironment::heap_free(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    return heap_allocs_.erase(ptr) > 0;
}

void Win32ShimEnvironment::set_env(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    env_vars_[name] = value;
}

std::optional<std::string> Win32ShimEnvironment::get_env(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = env_vars_.find(name);
    return it == env_vars_.end() ? std::nullopt : std::optional<std::string>(it->second);
}

void Win32ShimEnvironment::request_exit_process(uint32_t code) {
    exit_requested_ = true;
    exit_code_ = code;
    platform::Logger::instance().info("Win32Shim", "Guest called ExitProcess(" + std::to_string(code) + ")");
}

bool Win32ShimEnvironment::register_window_class(const std::string& class_name) {
    if (class_name.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(window_classes_.begin(), window_classes_.end(), class_name) == window_classes_.end()) {
        window_classes_.push_back(class_name);
    }
    return true;
}

uint64_t Win32ShimEnvironment::create_window(const std::string& class_name, const std::string& title,
                                             int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t hwnd = next_hwnd_++;
    VirtualWindow w;
    w.hwnd = hwnd;
    w.class_name = class_name;
    w.title = title;
    w.client_rect = {0, 0, width, height};
    windows_[hwnd] = std::move(w);
    return hwnd;
}

bool Win32ShimEnvironment::show_window(uint64_t hwnd, bool visible) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = windows_.find(hwnd);
    if (it == windows_.end()) return false;
    it->second.visible = visible;
    return true;
}

bool Win32ShimEnvironment::destroy_window(uint64_t hwnd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = windows_.find(hwnd);
    if (it == windows_.end()) return false;
    it->second.destroyed = true;
    msg_queue_.push_back({hwnd, kWmDestroy, 0, 0});
    return true;
}

std::optional<VirtualWindow> Win32ShimEnvironment::get_window(uint64_t hwnd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = windows_.find(hwnd);
    return it == windows_.end() ? std::nullopt : std::optional<VirtualWindow>(it->second);
}

void Win32ShimEnvironment::post_message(const VirtualWindowMessage& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    msg_queue_.push_back(msg);
}

void Win32ShimEnvironment::post_quit_message(int32_t exit_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    msg_queue_.push_back({0, kWmQuit, static_cast<uint64_t>(exit_code), 0});
}

bool Win32ShimEnvironment::peek_message(VirtualWindowMessage& out, bool remove) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (msg_queue_.empty()) return false;
    out = msg_queue_.front();
    if (remove) msg_queue_.pop_front();
    return true;
}

uint64_t Win32ShimEnvironment::reg_create_or_open_key(const std::string& full_path, bool create_if_missing) {
    if (create_if_missing) {
        if (!registry_.create_key(full_path)) return 0;
    } else if (!registry_.key_exists(full_path)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t hk = next_hkey_++;
    open_reg_keys_[hk] = VirtualRegistry::normalize_key_path(full_path);
    return hk;
}

bool Win32ShimEnvironment::reg_set_string(uint64_t hkey, const std::string& name, const std::string& value) {
    std::string kpath;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_reg_keys_.find(hkey);
        if (it == open_reg_keys_.end()) return false;
        kpath = it->second;
    }
    return registry_.set_value(kpath, name, RegValue::make_string(value));
}

bool Win32ShimEnvironment::reg_set_dword(uint64_t hkey, const std::string& name, uint32_t value) {
    std::string kpath;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_reg_keys_.find(hkey);
        if (it == open_reg_keys_.end()) return false;
        kpath = it->second;
    }
    return registry_.set_value(kpath, name, RegValue::make_dword(value));
}

std::optional<RegValue> Win32ShimEnvironment::reg_query_value(uint64_t hkey, const std::string& name) const {
    std::string kpath;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_reg_keys_.find(hkey);
        if (it == open_reg_keys_.end()) return std::nullopt;
        kpath = it->second;
    }
    return registry_.get_value(kpath, name);
}

bool Win32ShimEnvironment::reg_close_key(uint64_t hkey) {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_reg_keys_.erase(hkey) > 0;
}

void Win32ShimEnvironment::set_gamepad_snapshot(const input::XInputStateSnapshot& snapshot, bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    gamepad_snapshot_ = snapshot;
    gamepad_connected_ = connected;
}

bool Win32ShimEnvironment::xinput_get_state(uint32_t user_index, input::XInputStateSnapshot& out) const {
    if (user_index != 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!gamepad_connected_) return false;
    out = gamepad_snapshot_;
    return true;
}

} // namespace xwine::runtime
