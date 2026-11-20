#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace xwine::runtime {

enum class RegValueType : uint32_t {
    String = 1,   // REG_SZ
    Binary = 3,   // REG_BINARY
    Dword  = 4,   // REG_DWORD
    Qword  = 11   // REG_QWORD
};

struct RegValue {
    RegValueType type = RegValueType::String;
    std::variant<std::string, uint32_t, uint64_t, std::vector<uint8_t>> data;

    static RegValue make_string(std::string s) { return {RegValueType::String, std::move(s)}; }
    static RegValue make_dword(uint32_t v) { return {RegValueType::Dword, v}; }
    static RegValue make_qword(uint64_t v) { return {RegValueType::Qword, v}; }
    static RegValue make_binary(std::vector<uint8_t> b) { return {RegValueType::Binary, std::move(b)}; }
};

// Thread-safe in-process virtual Windows Registry persisted to LocalFolder/registry.json.
// Avoids blocked HKLM/HKCU OS registry writes in Xbox UWP AppContainer.
class VirtualRegistry {
public:
    VirtualRegistry();

    void seed_default_windows_keys();

    bool create_key(const std::string& key_path);
    bool key_exists(const std::string& key_path) const;
    bool delete_key(const std::string& key_path);

    bool set_value(const std::string& key_path, const std::string& value_name, const RegValue& val);
    std::optional<RegValue> get_value(const std::string& key_path, const std::string& value_name) const;
    bool delete_value(const std::string& key_path, const std::string& value_name);

    std::vector<std::string> list_subkeys(const std::string& parent_path) const;
    std::vector<std::string> list_value_names(const std::string& key_path) const;
    size_t total_key_count() const;
    size_t total_value_count() const;

    bool save_to_file(const std::filesystem::path& path, std::string* error_out = nullptr) const;
    bool load_from_file(const std::filesystem::path& path, std::string* error_out = nullptr);

    static std::string normalize_key_path(const std::string& raw);

private:
    mutable std::mutex mutex_;
    // normalized key path -> (lowercase value name -> {original name, RegValue})
    struct StoredValue {
        std::string display_name;
        RegValue value;
    };
    std::map<std::string, std::map<std::string, StoredValue>> keys_;
};

} // namespace xwine::runtime
