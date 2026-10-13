#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include "spinneret/domain/correlation_metadata.h"
#include "spinneret/domain/flight_event.h"
#include "spinneret/domain/message_types.h"
#include "spinneret/domain/yiapl_header.h"
#include "spinneret/pipeline/pipeline_error.h"

namespace spinneret::pipeline {

/// Outcome of handing one event to one output.
struct PublishReport {
    std::string output;
    std::string event_type;
    bool ok = false;
    bool required = false;
    std::string error;
};

/// Mutable state for one message as it moves through the pipeline. Each stage
/// reads what earlier stages produced and adds its own result.
class PipelineContext {
public:
    PipelineContext(std::string raw_input, domain::CorrelationMetadata correlation);

    const std::string& raw_input() const { return raw_input_; }
    const domain::CorrelationMetadata& correlation() const { return correlation_; }

    /// Appends an audit entry stamped with the elapsed time since ingress.
    void add_audit(std::string_view step);
    const std::vector<std::string>& audit_log() const { return audit_log_; }

    bool failed() const { return error.has_value(); }

    // ── Stage outputs ─────────────────────────────────────────────────────
    std::unique_ptr<pugi::xml_document> document;        // Stage 1
    std::optional<domain::YiaplHeader> header;           // Stage 1
    std::optional<domain::MessageFamily> family;         // Stage 2
    std::string intent;                                  // Stage 2
    std::optional<nlohmann::ordered_json> parsed;        // Stage 3
    std::vector<domain::FlightEvent> events;             // Stage 4
    std::vector<PublishReport> publish_reports;          // Publication
    std::optional<PipelineError> error;                  // Set on failure
    std::optional<domain::FlightEvent> error_event;      // PIPELINE_ERROR event, if one was built
    bool error_event_delivered = false;                  // Reached at least one output

private:
    std::string raw_input_;
    domain::CorrelationMetadata correlation_;
    std::chrono::steady_clock::time_point started_;
    std::vector<std::string> audit_log_;
};

}  // namespace spinneret::pipeline
