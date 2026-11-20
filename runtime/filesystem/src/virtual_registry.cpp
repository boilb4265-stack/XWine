#include "xwine/runtime/virtual_registry.hpp"
#include "xwine/platform/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace xwine::runtime {

namespace {

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string bytes_to_hex(const std::vector<uint8_t>& b) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(b.size() * 2);
    for (uint8_t v : b) {
        out.push_back(kHex[(v >> 4) & 0xF]);
        out.push_back(kHex[v & 0xF]);
    }
    return out;
}

std::vector<uint8_t> hex_to_bytes(const std::string& s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        int hi = nib(s[i]);
        int lo = nib(s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

} // namespace

VirtualRegistry::VirtualRegistry() {
    seed_default_windows_keys();
}

std::string VirtualRegistry::normalize_key_path(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    bool prev_sep = false;
    for (char c : raw) {
        if (c == '/' || c == '\\') {
            if (!out.empty() && !prev_sep) {
                out.push_back('\\');
                prev_sep = true;
            }
        } else {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            prev_sep = false;
        }
    }
    while (!out.empty() && out.back() == '\\') out.pop_back();

    // Canonicalize long Win32 root hive names to standard abbreviations
    auto replace_prefix = [&](const std::string& from, const std::string& to) {
        if (out == from) {
            out = to;
        } else if (out.rfind(from + "\\", 0) == 0) {
            out = to + out.substr(from.size());
        }
    };
    replace_prefix("HKEY_LOCAL_MACHINE", "HKLM");
    replace_prefix("HKEY_CURRENT_USER", "HKCU");
    replace_prefix("HKEY_CLASSES_ROOT", "HKCR");
    replace_prefix("HKEY_USERS", "HKU");
    return out;
}

void VirtualRegistry::seed_default_windows_keys() {
    set_value("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductName",
              RegValue::make_string("Windows 10 Pro (XWine Virtual Hive)"));
    set_value("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuild",
              RegValue::make_string("22621"));
    set_value("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentMajorVersionNumber",
              RegValue::make_dword(10));
    set_value("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentMinorVersionNumber",
              RegValue::make_dword(0));
    set_value("HKLM\\SOFTWARE\\XWine", "Version", RegValue::make_string("0.3.0"));
    set_value("HKLM\\SOFTWARE\\XWine", "SandboxMode", RegValue::make_string("AppContainer_Virtualized"));
    set_value("HKCU\\Control Panel\\Desktop", "LogPixels", RegValue::make_dword(96));
    set_value("HKCU\\Software\\XWine\\Audio", "PreferredBackend", RegValue::make_string("XAudio2_9"));
}

bool VirtualRegistry::create_key(const std::string& key_path) {
    const std::string norm = normalize_key_path(key_path);
    if (norm.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    // Ensure intermediate parent keys also exist
    size_t pos = 0;
    while (true) {
        size_t sep = norm.find('\\', pos);
        std::string prefix = (sep == std::string::npos) ? norm : norm.substr(0, sep);
        keys_[prefix];
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return true;
}

bool VirtualRegistry::key_exists(const std::string& key_path) const {
    const std::string norm = normalize_key_path(key_path);
    std::lock_guard<std::mutex> lock(mutex_);
    return keys_.find(norm) != keys_.end();
}

bool VirtualRegistry::delete_key(const std::string& key_path) {
    const std::string norm = normalize_key_path(key_path);
    if (norm.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    bool removed = false;
    const std::string prefix = norm + "\\";
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (it->first == norm || it->first.rfind(prefix, 0) == 0) {
            it = keys_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

bool VirtualRegistry::set_value(const std::string& key_path, const std::string& value_name,
                                const RegValue& val) {
    const std::string norm = normalize_key_path(key_path);
    if (norm.empty()) return false;
    create_key(norm);
    std::lock_guard<std::mutex> lock(mutex_);
    keys_[norm][to_lower_ascii(value_name)] = {value_name, val};
    return true;
}

std::optional<RegValue> VirtualRegistry::get_value(const std::string& key_path,
                                                   const std::string& value_name) const {
    const std::string norm = normalize_key_path(key_path);
    std::lock_guard<std::mutex> lock(mutex_);
    auto kit = keys_.find(norm);
    if (kit == keys_.end()) return std::nullopt;
    auto vit = kit->second.find(to_lower_ascii(value_name));
    if (vit == kit->second.end()) return std::nullopt;
    return vit->second.value;
}

bool VirtualRegistry::delete_value(const std::string& key_path, const std::string& value_name) {
    const std::string norm = normalize_key_path(key_path);
    std::lock_guard<std::mutex> lock(mutex_);
    auto kit = keys_.find(norm);
    if (kit == keys_.end()) return false;
    return kit->second.erase(to_lower_ascii(value_name)) > 0;
}

std::vector<std::string> VirtualRegistry::list_subkeys(const std::string& parent_path) const {
    const std::string norm = normalize_key_path(parent_path);
    const std::string prefix = norm + "\\";
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> direct;
    for (const auto& [k, _] : keys_) {
        if (k.rfind(prefix, 0) == 0) {
            std::string rest = k.substr(prefix.size());
            size_t slash = rest.find('\\');
            direct.insert(slash == std::string::npos ? rest : rest.substr(0, slash));
        }
    }
    return {direct.begin(), direct.end()};
}

std::vector<std::string> VirtualRegistry::list_value_names(const std::string& key_path) const {
    const std::string norm = normalize_key_path(key_path);
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    auto kit = keys_.find(norm);
    if (kit == keys_.end()) return out;
    for (const auto& [_, sv] : kit->second) {
        out.push_back(sv.display_name);
    }
    return out;
}

size_t VirtualRegistry::total_key_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return keys_.size();
}

size_t VirtualRegistry::total_value_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [_, m] : keys_) count += m.size();
    return count;
}

bool VirtualRegistry::save_to_file(const std::filesystem::path& path, std::string* error_out) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error_out) *error_out = "Failed creating registry parent dir: " + ec.message();
            return false;
        }
    }

    platform::JsonValue root = platform::JsonValue::make_object();
    root["schema_version"] = static_cast<uint64_t>(1);
    platform::JsonValue hives = platform::JsonValue::make_object();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [kpath, vals] : keys_) {
            platform::JsonValue kobj = platform::JsonValue::make_object();
            for (const auto& [_, sv] : vals) {
                platform::JsonValue vobj = platform::JsonValue::make_object();
                vobj["type"] = static_cast<uint64_t>(sv.value.type);
                switch (sv.value.type) {
                    case RegValueType::String:
                        vobj["data"] = std::get<std::string>(sv.value.data);
                        break;
                    case RegValueType::Dword:
                        vobj["data"] = static_cast<uint64_t>(std::get<uint32_t>(sv.value.data));
                        break;
                    case RegValueType::Qword:
                        vobj["data"] = std::to_string(std::get<uint64_t>(sv.value.data));
                        break;
                    case RegValueType::Binary:
                        vobj["data"] = bytes_to_hex(std::get<std::vector<uint8_t>>(sv.value.data));
                        break;
                }
                kobj[sv.display_name] = std::move(vobj);
            }
            hives[kpath] = std::move(kobj);
        }
    }
    root["keys"] = std::move(hives);

    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            if (error_out) *error_out = "Cannot open registry temp file for writing.";
            return false;
        }
        out << root.dump(2) << "\n";
        if (!out.good()) {
            if (error_out) *error_out = "Write failure saving virtual registry.";
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (error_out) *error_out = "Atomic rename failed for registry: " + ec.message();
        return false;
    }
    return true;
}

bool VirtualRegistry::load_from_file(const std::filesystem::path& path, std::string* error_out) {
    std::ifstream in(path);
    if (!in.is_open()) {
        if (error_out) *error_out = "Cannot open virtual registry file: " + path.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = platform::JsonValue::parse(ss.str());
    if (!parsed.ok || !parsed.value.is_object()) {
        if (error_out) *error_out = "Virtual registry JSON error: " + parsed.error;
        return false;
    }

    const auto* keys_obj = parsed.value.get("keys");
    if (!keys_obj || !keys_obj->is_object()) {
        if (error_out) *error_out = "Missing 'keys' object in registry JSON.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        keys_.clear();
    }

    for (const auto& [kpath, kobj] : keys_obj->as_object()) {
        create_key(kpath);
        if (!kobj.is_object()) continue;
        for (const auto& [vname, vobj] : kobj.as_object()) {
            if (!vobj.is_object()) continue;
            const auto* t = vobj.get("type");
            const auto* d = vobj.get("data");
            if (!t || !d) continue;
            const auto rtype = static_cast<RegValueType>(t->as_uint(1));
            switch (rtype) {
                case RegValueType::String:
                    set_value(kpath, vname, RegValue::make_string(d->as_string()));
                    break;
                case RegValueType::Dword:
                    set_value(kpath, vname, RegValue::make_dword(static_cast<uint32_t>(d->as_uint())));
                    break;
                case RegValueType::Qword:
                    set_value(kpath, vname,
                              RegValue::make_qword(std::strtoull(d->as_string().c_str(), nullptr, 10)));
                    break;
                case RegValueType::Binary:
                    set_value(kpath, vname, RegValue::make_binary(hex_to_bytes(d->as_string())));
                    break;
            }
        }
    }
    return true;
}

} // namespace xwine::runtime
