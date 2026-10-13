#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace spinneret::util {

/// Whole file as bytes, or nullopt if it does not exist. Throws on I/O errors.
std::optional<std::string> read_file(const std::filesystem::path& path);

/// Writes via a temporary file and rename, so readers never see a partial
/// file. With `owner_only`, restricts permissions to the owner (POSIX).
void atomic_write(const std::filesystem::path& path, std::string_view content, bool owner_only = false);

}  // namespace spinneret::util
