#include "spinneret/domain/flight_event.h"

namespace spinneret::domain {

nlohmann::ordered_json to_json(const FlightEvent& event) {
    nlohmann::ordered_json j;
    j["eventId"] = event.event_id;
    j["traceId"] = event.trace_id;
    j["eventType"] = event.event_type;
    j["businessKey"] = event.business_key;
    j["eventTime"] = util::iso8601_utc(event.event_time);
    j["payloadJson"] = event.payload;
    return j;
}

}  // namespace spinneret::domain
