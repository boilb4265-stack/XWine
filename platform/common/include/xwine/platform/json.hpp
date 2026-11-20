#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace xwine::platform {

class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue() : data_(nullptr) {}
    JsonValue(std::nullptr_t) : data_(nullptr) {}
    JsonValue(bool v) : data_(v) {}
    JsonValue(int v) : data_(static_cast<double>(v)) {}
    JsonValue(int64_t v) : data_(static_cast<double>(v)) {}
    JsonValue(uint64_t v) : data_(static_cast<double>(v)) {}
    JsonValue(double v) : data_(v) {}
    JsonValue(const char* v) : data_(std::string(v ? v : "")) {}
    JsonValue(std::string v) : data_(std::move(v)) {}
    JsonValue(Array v) : data_(std::move(v)) {}
    JsonValue(Object v) : data_(std::move(v)) {}

    static JsonValue make_object() { return JsonValue(Object{}); }
    static JsonValue make_array() { return JsonValue(Array{}); }

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_number() const { return std::holds_alternative<double>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<Array>(data_); }
    bool is_object() const { return std::holds_alternative<Object>(data_); }

    bool as_bool(bool fallback = false) const {
        return is_bool() ? std::get<bool>(data_) : fallback;
    }
    double as_double(double fallback = 0.0) const {
        return is_number() ? std::get<double>(data_) : fallback;
    }
    int64_t as_int(int64_t fallback = 0) const {
        return is_number() ? static_cast<int64_t>(std::get<double>(data_)) : fallback;
    }
    uint64_t as_uint(uint64_t fallback = 0) const {
        if (!is_number()) return fallback;
        double d = std::get<double>(data_);
        return d < 0.0 ? fallback : static_cast<uint64_t>(d);
    }
    const std::string& as_string() const {
        static const std::string empty;
        return is_string() ? std::get<std::string>(data_) : empty;
    }
    std::string as_string_or(const std::string& fallback) const {
        return is_string() ? std::get<std::string>(data_) : fallback;
    }
    const Array& as_array() const {
        static const Array empty;
        return is_array() ? std::get<Array>(data_) : empty;
    }
    Array& as_array_mut() {
        if (!is_array()) data_ = Array{};
        return std::get<Array>(data_);
    }
    const Object& as_object() const {
        static const Object empty;
        return is_object() ? std::get<Object>(data_) : empty;
    }
    Object& as_object_mut() {
        if (!is_object()) data_ = Object{};
        return std::get<Object>(data_);
    }

    bool has_key(const std::string& key) const {
        if (!is_object()) return false;
        const auto& obj = std::get<Object>(data_);
        return obj.find(key) != obj.end();
    }

    const JsonValue* get(const std::string& key) const {
        if (!is_object()) return nullptr;
        const auto& obj = std::get<Object>(data_);
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }

    JsonValue& operator[](const std::string& key) {
        return as_object_mut()[key];
    }

    void push_back(JsonValue val) {
        as_array_mut().push_back(std::move(val));
    }

    std::string dump(int indent = 2) const;

    struct ParseResult;

    static ParseResult parse(const std::string& input);

private:
    void dump_impl(std::string& out, int indent, int depth) const;
    Storage data_;
};

struct JsonValue::ParseResult {
    bool ok = false;
    JsonValue value;
    std::string error;
};

} // namespace xwine::platform
