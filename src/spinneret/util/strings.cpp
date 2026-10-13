#include "spinneret/util/strings.h"

#include <algorithm>
#include <cctype>

namespace spinneret::util {

namespace {
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
}  // namespace

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

std::string to_upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view local_name(std::string_view qualified_name) {
    auto colon = qualified_name.find(':');
    return colon == std::string_view::npos ? qualified_name : qualified_name.substr(colon + 1);
}

std::string_view name_prefix(std::string_view qualified_name) {
    auto colon = qualified_name.find(':');
    return colon == std::string_view::npos ? std::string_view{} : qualified_name.substr(0, colon);
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

}  // namespace spinneret::util
