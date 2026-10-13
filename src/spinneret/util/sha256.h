#pragma once

#include <string>
#include <string_view>

namespace spinneret::util {

/// Lower-case hex SHA-256 digest of `data`.
std::string sha256_hex(std::string_view data);

}  // namespace spinneret::util
