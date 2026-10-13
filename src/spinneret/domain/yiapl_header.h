#pragma once

#include <optional>
#include <string>

#include "spinneret/util/time.h"

namespace spinneret::domain {

/// Fields of the `yiapl:YIAPL_Header` block. Values are whitespace-trimmed;
/// absent elements are empty strings.
struct YiaplHeader {
    std::string sequence_number;
    std::string message_type;
    std::string message_sub_type;
    std::string sending_system;
    std::string originator_timestamp;  ///< Raw text as received.
    std::optional<util::Clock::time_point> originator_time;  ///< Parsed, when valid ISO-8601.
    bool message_type_deduced = false;  ///< True when MessageType came from the root element name.
};

}  // namespace spinneret::domain
