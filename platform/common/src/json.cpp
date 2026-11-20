#include "xwine/platform/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace xwine::platform {

namespace {

void escape_json_string(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : src_(src) {}

    JsonValue::ParseResult parse() {
        skip_ws();
        JsonValue val;
        if (!parse_value(val, 0)) {
            return {false, JsonValue{}, error_};
        }
        skip_ws();
        if (pos_ != src_.size()) {
            return {false, JsonValue{}, "Trailing characters after JSON root value"};
        }
        return {true, std::move(val), ""};
    }

private:
    static constexpr int kMaxDepth = 128;

    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parse_value(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            error_ = "Maximum JSON nesting depth exceeded";
            return false;
        }
        skip_ws();
        if (pos_ >= src_.size()) {
            error_ = "Unexpected end of JSON input";
            return false;
        }
        char c = src_[pos_];
        if (c == '{') return parse_object(out, depth + 1);
        if (c == '[') return parse_array(out, depth + 1);
        if (c == '"') {
            std::string s;
            if (!parse_string(s)) return false;
            out = JsonValue(std::move(s));
            return true;
        }
        if (c == 't') return parse_literal("true", JsonValue(true), out);
        if (c == 'f') return parse_literal("false", JsonValue(false), out);
        if (c == 'n') return parse_literal("null", JsonValue(nullptr), out);
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);

        error_ = std::string("Unexpected character in JSON: '") + c + "'";
        return false;
    }

    bool parse_literal(const char* lit, JsonValue val, JsonValue& out) {
        size_t len = std::char_traits<char>::length(lit);
        if (src_.compare(pos_, len, lit) != 0) {
            error_ = std::string("Invalid literal, expected ") + lit;
            return false;
        }
        pos_ += len;
        out = std::move(val);
        return true;
    }

    bool parse_string(std::string& out) {
        if (pos_ >= src_.size() || src_[pos_] != '"') {
            error_ = "Expected '\"'";
            return false;
        }
        ++pos_;
        out.clear();
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) {
                error_ = "Unescaped control character in JSON string";
                return false;
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    error_ = "Unterminated escape sequence";
                    return false;
                }
                char esc = src_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) {
                            error_ = "Incomplete \\uXXXX escape";
                            return false;
                        }
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                            else {
                                error_ = "Invalid hex digit in \\u escape";
                                return false;
                            }
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        error_ = std::string("Invalid escape character '\\") + esc + "'";
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        error_ = "Unterminated JSON string";
        return false;
    }

    bool parse_number(JsonValue& out) {
        size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        if (pos_ >= src_.size()) {
            error_ = "Incomplete number";
            return false;
        }
        if (src_[pos_] == '0') {
            ++pos_;
        } else if (src_[pos_] >= '1' && src_[pos_] <= '9') {
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
        } else {
            error_ = "Invalid number format";
            return false;
        }
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9') {
                error_ = "Invalid fractional number";
                return false;
            }
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9') {
                error_ = "Invalid exponent";
                return false;
            }
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
        }
        std::string token = src_.substr(start, pos_ - start);
        char* end_ptr = nullptr;
        double val = std::strtod(token.c_str(), &end_ptr);
        if (!end_ptr || *end_ptr != '\0' || !std::isfinite(val)) {
            error_ = "Non-finite or malformed JSON number";
            return false;
        }
        out = JsonValue(val);
        return true;
    }

    bool parse_array(JsonValue& out, int depth) {
        ++pos_; // '['
        JsonValue::Array arr;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == ']') {
            ++pos_;
            out = JsonValue(std::move(arr));
            return true;
        }
        while (pos_ < src_.size()) {
            JsonValue elem;
            if (!parse_value(elem, depth)) return false;
            arr.push_back(std::move(elem));
            skip_ws();
            if (pos_ >= src_.size()) {
                error_ = "Unterminated JSON array";
                return false;
            }
            if (src_[pos_] == ']') {
                ++pos_;
                out = JsonValue(std::move(arr));
                return true;
            }
            if (src_[pos_] != ',') {
                error_ = "Expected ',' or ']' in JSON array";
                return false;
            }
            ++pos_;
        }
        error_ = "Unterminated JSON array";
        return false;
    }

    bool parse_object(JsonValue& out, int depth) {
        ++pos_; // '{'
        JsonValue::Object obj;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '}') {
            ++pos_;
            out = JsonValue(std::move(obj));
            return true;
        }
        while (pos_ < src_.size()) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != ':') {
                error_ = "Expected ':' after object key";
                return false;
            }
            ++pos_;
            JsonValue val;
            if (!parse_value(val, depth)) return false;
            obj[std::move(key)] = std::move(val);
            skip_ws();
            if (pos_ >= src_.size()) {
                error_ = "Unterminated JSON object";
                return false;
            }
            if (src_[pos_] == '}') {
                ++pos_;
                out = JsonValue(std::move(obj));
                return true;
            }
            if (src_[pos_] != ',') {
                error_ = "Expected ',' or '}' in JSON object";
                return false;
            }
            ++pos_;
        }
        error_ = "Unterminated JSON object";
        return false;
    }

    const std::string& src_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

std::string JsonValue::dump(int indent) const {
    std::string out;
    dump_impl(out, indent, 0);
    return out;
}

void JsonValue::dump_impl(std::string& out, int indent, int depth) const {
    if (is_null()) {
        out += "null";
    } else if (is_bool()) {
        out += std::get<bool>(data_) ? "true" : "false";
    } else if (is_number()) {
        double d = std::get<double>(data_);
        if (!std::isfinite(d)) {
            out += "null";
        } else if (std::floor(d) == d && std::abs(d) < 9007199254740992.0) {
            out += std::to_string(static_cast<int64_t>(d));
        } else {
            std::ostringstream oss;
            oss << std::setprecision(12) << d;
            out += oss.str();
        }
    } else if (is_string()) {
        escape_json_string(std::get<std::string>(data_), out);
    } else if (is_array()) {
        const auto& arr = std::get<Array>(data_);
        if (arr.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        for (size_t i = 0; i < arr.size(); ++i) {
            if (indent > 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>((depth + 1) * indent), ' ');
            }
            arr[i].dump_impl(out, indent, depth + 1);
            if (i + 1 < arr.size()) out.push_back(',');
        }
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(depth * indent), ' ');
        }
        out.push_back(']');
    } else if (is_object()) {
        const auto& obj = std::get<Object>(data_);
        if (obj.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        size_t idx = 0;
        for (const auto& [k, v] : obj) {
            if (indent > 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>((depth + 1) * indent), ' ');
            }
            escape_json_string(k, out);
            out += (indent > 0) ? ": " : ":";
            v.dump_impl(out, indent, depth + 1);
            if (++idx < obj.size()) out.push_back(',');
        }
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(depth * indent), ' ');
        }
        out.push_back('}');
    }
}

JsonValue::ParseResult JsonValue::parse(const std::string& input) {
    JsonParser parser(input);
    return parser.parse();
}

} // namespace xwine::platform
