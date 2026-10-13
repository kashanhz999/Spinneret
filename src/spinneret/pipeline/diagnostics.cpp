#include "spinneret/pipeline/diagnostics.h"

namespace spinneret::pipeline {

nlohmann::ordered_json diagnostic_report(const PipelineContext& context, bool published) {
    nlohmann::ordered_json report;
    report["status"] = context.failed() ? "FAILED" : "SUCCESS";
    if (context.error) {
        report["errorKind"] = to_string(context.error->kind());
        report["error"] = context.error->what();
    }
    report["traceId"] = context.correlation().trace_id;
    report["intent"] = context.intent.empty() ? "UNKNOWN" : context.intent;

    if (context.header) {
        const auto& h = *context.header;
        report["header"] = {
            {"sequenceNumber", h.sequence_number},
            {"messageType", h.message_type},
            {"messageSubType", h.message_sub_type},
            {"sendingSystem", h.sending_system},
            {"originatorTimeStamp", h.originator_timestamp},
            {"messageTypeDeduced", h.message_type_deduced},
        };
    }
    if (context.parsed) report["parsedInput"] = *context.parsed;

    auto events = nlohmann::ordered_json::array();
    for (const auto& event : context.events) events.push_back(domain::to_json(event));
    report["events"] = std::move(events);
    if (context.error_event) report["errorEvent"] = domain::to_json(*context.error_event);

    report["published"] = published;
    auto deliveries = nlohmann::ordered_json::array();
    for (const auto& r : context.publish_reports) {
        nlohmann::ordered_json d{{"output", r.output}, {"eventType", r.event_type}, {"ok", r.ok},
                                 {"required", r.required}};
        if (!r.ok) d["error"] = r.error;
        deliveries.push_back(std::move(d));
    }
    report["deliveries"] = std::move(deliveries);
    report["auditLog"] = context.audit_log();
    return report;
}

}  // namespace spinneret::pipeline
