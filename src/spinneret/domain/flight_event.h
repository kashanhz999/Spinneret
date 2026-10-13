#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "spinneret/util/time.h"

namespace spinneret::domain {

/// The canonical event published to every output.
///
/// Its JSON form is the wire contract shared with downstream consumers and is
/// identical to SXPM's `FlightEventEntity`:
/// `{eventId, traceId, eventType, businessKey, eventTime, payloadJson}`, where
/// `payloadJson` is the full XML tree as a JSON object (not a string).
struct FlightEvent {
    std::string event_id;
    std::string trace_id;
    std::string event_type;  ///< Business intent, e.g. FLIGHT_GATE_UPDATE, or PIPELINE_ERROR.
    std::string business_key;
    util::Clock::time_point event_time;
    nlohmann::ordered_json payload;
};

inline constexpr const char* kPipelineErrorEventType = "PIPELINE_ERROR";

nlohmann::ordered_json to_json(const FlightEvent& event);

}  // namespace spinneret::domain
