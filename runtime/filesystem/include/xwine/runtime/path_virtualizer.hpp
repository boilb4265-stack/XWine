#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace xwine::runtime {

struct VirtualPathResult {
    bool allowed = false;
    std::filesystem::path host_path;
    std::string normalized_windows_path;
    std::string rejection_reason;
};

// Maps Windows paths (C:\Windows\..., C:\Users\XWine\..., relative paths) strictly
// into an Xbox AppContainer-compatible sandbox root directory (drive_c/, games/, saves/).
class PathVirtualizer {
public:
    explicit PathVirtualizer(std::filesystem::path prefix_root);

    bool ensure_prefix_layout() const;

    VirtualPathResult resolve_windows_path(const std::string& win_path,
                                           const std::string& virtual_cwd = "C:\\") const;

    const std::filesystem::path& prefix_root() const { return prefix_root_; }
    std::filesystem::path drive_c_root() const { return prefix_root_ / "drive_c"; }

private:
    static std::filesystem::path resolve_case_insensitive(const std::filesystem::path& root,
                                                          const std::filesystem::path& relative);

    std::filesystem::path prefix_root_;
};

} // namespace xwine::runtime
