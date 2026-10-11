// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_visitor.hpp"
#include <json-c/json.h>
#include <algorithm>
namespace tube5
{
bool youtube_visitor(std::string_view json, std::array<char, 4097> &visitor) noexcept
{
    visitor = {};
    if (json.empty() || json.size() > 16384)
        return false;
    auto *tokener = json_tokener_new_ex(16);
    if (!tokener)
        return false;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    auto *root = json_tokener_parse_ex(tokener, json.data(), static_cast<int>(json.size()));
    const bool valid = json_tokener_get_error(tokener) == json_tokener_success &&
                       json_tokener_get_parse_end(tokener) == json.size();
    json_tokener_free(tokener);
    json_object *context = nullptr, *value = nullptr;
    bool ok = valid && root && json_object_is_type(root, json_type_object) &&
              json_object_object_get_ex(root, "responseContext", &context) &&
              json_object_is_type(context, json_type_object) &&
              json_object_object_get_ex(context, "visitorData", &value) &&
              json_object_is_type(value, json_type_string);
    if (ok)
    {
        const std::string_view text(json_object_get_string(value),
                                    json_object_get_string_len(value));
        ok = !text.empty() && text.size() < visitor.size();
        for (const char c : text)
            ok &= (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '=' || c == '%';
        if (ok)
            std::copy(text.begin(), text.end(), visitor.begin());
    }
    if (root)
        json_object_put(root);
    return ok;
}
} // namespace tube5
