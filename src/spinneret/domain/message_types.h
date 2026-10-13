#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace spinneret::domain {

/// The four YIAPL message families produced by the AODB.
enum class MessageFamily {
    MSTR,  ///< Master / reference data.
    FLSH,  ///< Flight schedule (daily snapshot or ad hoc).
    FLTU,  ///< Real-time flight updates.
    HLTH,  ///< Health-check heartbeats.
};

using IntentEntry = std::pair<std::string_view, std::string_view>;  // (subType, intent)

/// Parses an upper-case family code ("FLTU" → FLTU).
std::optional<MessageFamily> parse_family(std::string_view code);

std::string_view to_string(MessageFamily family);

/// Every (subType, intent) pair defined for a family. This table is the single
/// source of truth for supported subtypes.
std::span<const IntentEntry> intents(MessageFamily family);

/// Business intent for an upper-case subtype, or nullopt if unsupported.
std::optional<std::string_view> resolve_intent(MessageFamily family, std::string_view sub_type);

}  // namespace spinneret::domain
