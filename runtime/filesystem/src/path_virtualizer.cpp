#include "xwine/runtime/path_virtualizer.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace xwine::runtime {

namespace {

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split_components(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == '\\' || c == '/') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

} // namespace

PathVirtualizer::PathVirtualizer(std::filesystem::path prefix_root)
    : prefix_root_(std::move(prefix_root)) {}

bool PathVirtualizer::ensure_prefix_layout() const {
    std::error_code ec;
    const std::vector<std::filesystem::path> dirs = {
        prefix_root_,
        drive_c_root(),
        drive_c_root() / "Windows" / "System32",
        drive_c_root() / "Program Files",
        drive_c_root() / "Users" / "XWine" / "AppData" / "Local",
        drive_c_root() / "Users" / "XWine" / "Documents"
    };
    for (const auto& d : dirs) {
        std::filesystem::create_directories(d, ec);
        if (ec) return false;
    }
    return true;
}

std::filesystem::path PathVirtualizer::resolve_case_insensitive(const std::filesystem::path& root,
                                                                const std::filesystem::path& relative) {
    std::filesystem::path cur = root;
    std::error_code ec;
    for (const auto& comp : relative) {
        const std::string wanted = comp.string();
        if (wanted.empty() || wanted == ".") continue;
        std::filesystem::path direct = cur / wanted;
        if (std::filesystem::exists(direct, ec)) {
            cur = direct;
            continue;
        }
        bool matched = false;
        if (std::filesystem::is_directory(cur, ec)) {
            const std::string low_wanted = to_lower_ascii(wanted);
            for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
                if (to_lower_ascii(entry.path().filename().string()) == low_wanted) {
                    cur = entry.path();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            cur /= wanted;
        }
    }
    return cur;
}

VirtualPathResult PathVirtualizer::resolve_windows_path(const std::string& win_path,
                                                        const std::string& virtual_cwd) const {
    VirtualPathResult res;
    if (win_path.empty()) {
        res.rejection_reason = "Empty Windows path.";
        return res;
    }

    std::string p = win_path;
    // Strip Win32 extended-length prefix \\?\ if present
    if (p.rfind("\\\\?\\", 0) == 0) {
        p = p.substr(4);
    }
    // Reject UNC network paths and raw device paths (\\.\PhysicalDrive0)
    if (p.rfind("\\\\", 0) == 0) {
        res.rejection_reason = "UNC and raw device paths are blocked in Xbox AppContainer sandbox.";
        return res;
    }

    char drive = 'C';
    std::string rest;

    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') {
        drive = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
        rest = p.substr(2);
    } else if (!p.empty() && (p[0] == '\\' || p[0] == '/')) {
        rest = p;
    } else {
        // Relative to virtual_cwd
        std::string cwd = virtual_cwd;
        if (cwd.size() >= 2 && std::isalpha(static_cast<unsigned char>(cwd[0])) && cwd[1] == ':') {
            drive = static_cast<char>(std::toupper(static_cast<unsigned char>(cwd[0])));
            cwd = cwd.substr(2);
        }
        rest = cwd + "\\" + p;
    }

    if (drive != 'C') {
        res.rejection_reason = std::string("Drive '") + drive +
                               ":' is not mounted inside XWine AppContainer prefix (only virtual C: is mapped).";
        return res;
    }

    // Safely resolve '.' and '..' components without ever escaping drive C: root
    std::vector<std::string> stack;
    for (const auto& comp : split_components(rest)) {
        if (comp == ".") continue;
        if (comp == "..") {
            if (stack.empty()) {
                res.rejection_reason = "Path traversal above C:\\ root blocked by sandbox.";
                return res;
            }
            stack.pop_back();
            continue;
        }
        // Reject embedded NUL or colon (NTFS alternate data streams)
        if (comp.find(':') != std::string::npos) {
            res.rejection_reason = "Invalid ':' character in path component.";
            return res;
        }
        stack.push_back(comp);
    }

    std::filesystem::path rel;
    std::string norm_win = "C:";
    for (const auto& s : stack) {
        rel /= s;
        norm_win += "\\" + s;
    }
    if (stack.empty()) norm_win = "C:\\";

    const auto root = drive_c_root();
    const auto resolved = resolve_case_insensitive(root, rel);

    // Final canonical boundary check
    const auto canon_root = std::filesystem::weakly_canonical(root);
    const auto canon_res = std::filesystem::weakly_canonical(resolved);
    auto rit = canon_root.begin();
    auto cit = canon_res.begin();
    for (; rit != canon_root.end(); ++rit, ++cit) {
        if (cit == canon_res.end() || *rit != *cit) {
            res.rejection_reason = "Resolved path escapes sandbox drive_c boundary.";
            return res;
        }
    }

    res.allowed = true;
    res.host_path = resolved;
    res.normalized_windows_path = norm_win;
    return res;
}

} // namespace xwine::runtime
