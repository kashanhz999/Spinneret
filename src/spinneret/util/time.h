#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace spinneret::util {

using Clock = std::chrono::system_clock;

/// Formats a time point as ISO-8601 UTC with millisecond precision,
/// e.g. `2024-05-27T06:47:16.533Z`.
std::string iso8601_utc(Clock::time_point tp);

/// Parses `yyyy-MM-ddTHH:mm:ss[.fff…][Z]` (treated as UTC).
std::optional<Clock::time_point> parse_iso8601(std::string_view text);

/// Milliseconds since the Unix epoch.
long long epoch_millis(Clock::time_point tp);

}  // namespace spinneret::util
