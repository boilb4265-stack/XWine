#include "xwine/steam/vdf_parser.hpp"

#include <algorithm>
#include <cctype>

namespace xwine::steam {

namespace {

std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src) {}

    VdfParseResult run() {
        VdfNode root;
        root.key = "<root>";
        root.is_section = true;

        while (true) {
            skip_ws_and_comments();
            if (pos_ >= src_.size()) break;
            VdfNode child;
            if (!parse_entry(child, 0)) {
                return {false, VdfNode{}, error_};
            }
            root.children.push_back(std::move(child));
        }
        return {true, std::move(root), ""};
    }

private:
    static constexpr int kMaxDepth = 64;

    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
                pos_ += 2;
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
            } else {
                break;
            }
        }
    }

    bool read_token(std::string& out) {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            error_ = "Unexpected end of VDF input";
            return false;
        }
        out.clear();
        if (src_[pos_] == '"') {
            ++pos_;
            while (pos_ < src_.size()) {
                char c = src_[pos_++];
                if (c == '"') return true;
                if (c == '\\' && pos_ < src_.size()) {
                    char esc = src_[pos_++];
                    switch (esc) {
                        case 'n': out.push_back('\n'); break;
                        case 't': out.push_back('\t'); break;
                        case '\\': out.push_back('\\'); break;
                        case '"': out.push_back('"'); break;
                        default: out.push_back(esc); break;
                    }
                } else {
                    out.push_back(c);
                }
            }
            error_ = "Unterminated quoted string in VDF";
            return false;
        }
        if (src_[pos_] == '{' || src_[pos_] == '}') {
            error_ = std::string("Unexpected brace '") + src_[pos_] + "' where token was expected";
            return false;
        }
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"') {
                break;
            }
            out.push_back(c);
            ++pos_;
        }
        return !out.empty();
    }

    bool parse_entry(VdfNode& node, int depth) {
        if (depth > kMaxDepth) {
            error_ = "VDF nesting depth exceeded";
            return false;
        }
        if (!read_token(node.key)) return false;
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            error_ = "Expected value or '{' after key '" + node.key + "'";
            return false;
        }
        if (src_[pos_] == '{') {
            ++pos_;
            node.is_section = true;
            while (true) {
                skip_ws_and_comments();
                if (pos_ >= src_.size()) {
                    error_ = "Unterminated VDF section '" + node.key + "'";
                    return false;
                }
                if (src_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                VdfNode child;
                if (!parse_entry(child, depth + 1)) return false;
                node.children.push_back(std::move(child));
            }
        } else {
            node.is_section = false;
            return read_token(node.value);
        }
    }

    const std::string& src_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

const VdfNode* VdfNode::find_child(const std::string& name) const {
    const std::string needle = lower_ascii(name);
    for (const auto& c : children) {
        if (lower_ascii(c.key) == needle) {
            return &c;
        }
    }
    return nullptr;
}

std::string VdfNode::child_value(const std::string& name, const std::string& fallback) const {
    const auto* c = find_child(name);
    if (!c || c->is_section) return fallback;
    return c->value;
}

VdfParseResult VdfParser::parse(const std::string& text) {
    Parser p(text);
    return p.run();
}

} // namespace xwine::steam
