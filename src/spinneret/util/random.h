#pragma once

#include <cstddef>
#include <string>

namespace spinneret::util {

/// Random RFC 4122 version-4 UUID, e.g. `3f2b…-4…-a…`.
std::string random_uuid();

/// Hex string of `bytes` bytes drawn from the OS entropy source
/// (suitable for secrets such as API keys).
std::string random_hex(std::size_t bytes);

}  // namespace spinneret::util
