#pragma once

#include <string>
#include <string_view>

namespace spinneret::util {

/// Returns `s` without leading and trailing ASCII whitespace.
std::string_view trim(std::string_view s);

/// ASCII upper-casing.
std::string to_upper(std::string_view s);

/// ASCII lower-casing.
std::string to_lower(std::string_view s);

/// Part of a qualified XML name after the colon ("aidx:Airline" → "Airline").
std::string_view local_name(std::string_view qualified_name);

/// Part of a qualified XML name before the colon, or empty when unprefixed.
std::string_view name_prefix(std::string_view qualified_name);

bool starts_with(std::string_view s, std::string_view prefix);
bool contains_ci(std::string_view haystack, std::string_view needle);

}  // namespace spinneret::util
