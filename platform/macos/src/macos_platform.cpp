#include "xwine/platform/platform.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace xwine::platform {

namespace {

uint64_t calculate_directory_size(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return 0;
    uint64_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

class MacOSPlatform final : public IPlatform {
public:
    MacOSPlatform()
        : start_time_(std::chrono::steady_clock::now()),
          last_cpu_sample_time_(start_time_) {
        sample_cpu_usage_baseline();
    }

    PlatformId platform_id() const override {
#if defined(__aarch64__) || defined(__arm64__)
        return PlatformId::MacOS_Arm64;
#else
        return PlatformId::MacOS_X64;
#endif
    }

    HardwareProfile host_hardware() const override {
        HardwareProfile p;
        p.device_name = "Apple Mac Development Workstation";
#if defined(__aarch64__) || defined(__arm64__)
        p.cpu_architecture = "arm64 (Apple Silicon)";
        p.gpu_name = "Apple Silicon Integrated GPU (Metal)";
        p.native_x86_64_execution = false;
#else
        p.cpu_architecture = "x86_64";
        p.gpu_name = "macOS Metal GPU";
        p.native_x86_64_execution = true;
#endif
        p.supports_dynamic_code_gen = true;
        p.hardware_verified = true;

#if defined(__APPLE__)
        char brand[256]{};
        size_t brand_len = sizeof(brand);
        if (sysctlbyname("machdep.cpu.brand_string", brand, &brand_len, nullptr, 0) == 0 && brand[0] != '\0') {
            p.cpu_model = brand;
        } else {
            p.cpu_model = "Apple Silicon";
        }

        uint32_t cores = 0;
        size_t cores_len = sizeof(cores);
        if (sysctlbyname("hw.logicalcpu", &cores, &cores_len, nullptr, 0) == 0) {
            p.logical_cores = cores;
        }

        uint64_t mem_bytes = 0;
        size_t mem_len = sizeof(mem_bytes);
        if (sysctlbyname("hw.memsize", &mem_bytes, &mem_len, nullptr, 0) == 0) {
            p.physical_ram_bytes = mem_bytes;
            p.app_usable_ram_bytes = mem_bytes;
        }
#endif
        p.supported_graphics_apis = {"Metal (Native via SDL3)"};
        p.unsupported_graphics_apis = {"Direct3D 9/10/11/12 (No native macOS D3D)", "Native Vulkan (Requires MoltenVK)"};
        return p;
    }

    HardwareProfile target_xbox_series_s_profile(bool game_mode) const override {
        return get_xbox_series_s_hardware_profile(game_mode);
    }

    std::filesystem::path default_data_directory() const override {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            return std::filesystem::path(home) / "Library" / "Application Support" / "XWine";
        }
        return std::filesystem::current_path() / ".xwine_data";
    }

    StorageInfo query_storage(const std::filesystem::path& path) const override {
        StorageInfo info;
        info.root_path = path;
        info.sandboxed = false;
        info.filesystem_notes = "macOS APFS workstation directory (mirrors Xbox LocalFolder layout)";

        std::error_code ec;
        std::filesystem::path probe = path;
        if (!std::filesystem::exists(probe, ec)) {
            std::filesystem::create_directories(probe, ec);
        }
        if (!ec && std::filesystem::exists(probe, ec)) {
            auto sp = std::filesystem::space(probe, ec);
            if (!ec) {
                info.total_bytes = sp.capacity;
                info.available_bytes = sp.available;
            }
            info.used_by_xwine_bytes = calculate_directory_size(probe);

            // Non-destructive write check
            auto test_file = probe / ".xwine_write_probe";
            std::ofstream ofs(test_file);
            if (ofs.is_open()) {
                ofs.close();
                std::filesystem::remove(test_file, ec);
                info.writable = true;
            }
        }
        return info;
    }

    SystemMetrics query_metrics(uint64_t target_cap_bytes) const override {
        SystemMetrics m;
        m.target_memory_cap_bytes = target_cap_bytes;
        const auto now = std::chrono::steady_clock::now();
        m.uptime_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());

#if defined(__APPLE__)
        uint64_t mem_bytes = 0;
        size_t mem_len = sizeof(mem_bytes);
        if (sysctlbyname("hw.memsize", &mem_bytes, &mem_len, nullptr, 0) == 0) {
            m.total_system_ram_bytes = mem_bytes;
        }

        mach_task_basic_info_data_t task_info_data{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&task_info_data), &count) == KERN_SUCCESS) {
            m.process_rss_bytes = static_cast<uint64_t>(task_info_data.resident_size);
        }

        std::lock_guard<std::mutex> lock(cpu_mutex_);
        struct rusage ru{};
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
            const double cpu_sec =
                (static_cast<double>(ru.ru_utime.tv_sec) + static_cast<double>(ru.ru_utime.tv_usec) * 1e-6) +
                (static_cast<double>(ru.ru_stime.tv_sec) + static_cast<double>(ru.ru_stime.tv_usec) * 1e-6);
            const double wall_sec =
                std::chrono::duration<double>(now - last_cpu_sample_time_).count();
            if (wall_sec >= 0.25) {
                const double delta_cpu = cpu_sec - last_cpu_seconds_;
                last_cpu_percent_ = (delta_cpu / wall_sec) * 100.0;
                if (last_cpu_percent_ < 0.0) last_cpu_percent_ = 0.0;
                last_cpu_seconds_ = cpu_sec;
                last_cpu_sample_time_ = now;
            }
            m.process_cpu_percent = last_cpu_percent_;
        }
        // macOS does not expose a public non-privileged per-process GPU utilization API;
        // leave m.gpu_utilization_percent as std::nullopt rather than fabricating a number.
#endif
        return m;
    }

    std::vector<PlatformCapability> capability_matrix() const override {
        return get_xbox_dev_mode_capabilities();
    }

private:
    void sample_cpu_usage_baseline() {
#if defined(__APPLE__)
        struct rusage ru{};
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
            last_cpu_seconds_ =
                (static_cast<double>(ru.ru_utime.tv_sec) + static_cast<double>(ru.ru_utime.tv_usec) * 1e-6) +
                (static_cast<double>(ru.ru_stime.tv_sec) + static_cast<double>(ru.ru_stime.tv_usec) * 1e-6);
        }
#endif
    }

    std::chrono::steady_clock::time_point start_time_;
    mutable std::mutex cpu_mutex_;
    mutable std::chrono::steady_clock::time_point last_cpu_sample_time_;
    mutable double last_cpu_seconds_ = 0.0;
    mutable double last_cpu_percent_ = 0.0;
};

} // namespace

#if !defined(XWINE_PLATFORM_XBOX)
std::unique_ptr<IPlatform> create_host_platform() {
    return std::make_unique<MacOSPlatform>();
}
#endif

} // namespace xwine::platform
