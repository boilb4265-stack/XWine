#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xwine::steam {

struct VdfNode {
    std::string key;
    std::string value;
    bool is_section = false;
    std::vector<VdfNode> children;

    const VdfNode* find_child(const std::string& name) const;
    std::string child_value(const std::string& name, const std::string& fallback = "") const;
};

struct VdfParseResult {
    bool ok = false;
    VdfNode root;
    std::string error;
};

class VdfParser {
public:
    static VdfParseResult parse(const std::string& text);
};

} // namespace xwine::steam
