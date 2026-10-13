#include "spinneret/pipeline/pipeline_engine.h"

#include <chrono>
#include <format>

#include "spinneret/pipeline/xml_format.h"
#include "spinneret/util/logging.h"
#include "spinneret/util/random.h"

namespace spinneret::pipeline {

namespace {

const util::Logger kLog{"pipeline.engine"};

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string out;
    for (const auto& part : parts) {
        if (!out.empty()) out += separator;
        out += part;
    }
    return out;
}

bool wants_error_event(ErrorEventPolicy policy, ErrorKind kind) {
    switch (policy) {
        case ErrorEventPolicy::Always: return true;
        case ErrorEventPolicy::PermanentOnly: return is_permanent(kind);
        case ErrorEventPolicy::Never: return false;
    }
    return false;
}

}  // namespace

PipelineEngine::PipelineEngine(TransformerRegistry transformers, const registry::AdapterRegistry& registry,
                               metrics::Metrics& metrics)
    : transformers_(std::move(transformers)), registry_(registry), metrics_(metrics) {}

std::vector<std::string> PipelineEngine::enabled_outputs() const {
    std::vector<std::string> names;
    for (auto* output : registry_.outputs()) {
        if (output->enabled()) names.push_back(output->name());
    }
    return names;
}

void PipelineEngine::ingest(PipelineContext& context, const IngestOptions& options) const {
    util::TraceScope trace(context.correlation().trace_id);
    const auto started = std::chrono::steady_clock::now();

    try {
        run_stages(context, options);
    } catch (const PipelineError& e) {
        context.error = e;
    } catch (const std::exception& e) {
        context.error = PipelineError(ErrorKind::Internal, e.what());
    } catch (...) {
        context.error = PipelineError(ErrorKind::Internal, "Unknown error");
    }

    const double latency =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    if (context.error) {
        const ErrorKind kind = context.error->kind();
        metrics_.record_failure(kind, latency);
        context.add_audit(std::format("FAILED ({}): {}", to_string(kind), context.error->what()));
        kLog.error("Pipeline failed [{}]: {}", to_string(kind), context.error->what());

        if (options.publish && wants_error_event(options.error_events, kind)) {
            publish_error_event(context);
        }
    } else {
        metrics_.record_success(latency);
    }

    // Stage 6: Audit
    kLog.info("Pipeline {}: {}", context.failed() ? "Failed" : "Complete", join(context.audit_log(), " -> "));
}

void PipelineEngine::run_stages(PipelineContext& context, const IngestOptions& options) const {
    header_extractor_.execute(context);   // Stage 1
    intent_resolver_.execute(context);    // Stage 2
    payload_extractor_.execute(context);  // Stage 3
    transformers_.execute(context);       // Stage 4

    // Stage 5: the trace ID is bound for the whole ingest() call (see TraceScope).
    context.add_audit(std::format("Stage 5: Correlation (TraceID={})", context.correlation().trace_id));

    if (options.publish) {
        publish_events(context);
    } else {
        context.add_audit("Publication skipped (dry run)");
    }
}

void PipelineEngine::publish_events(PipelineContext& context) const {
    if (context.events.empty()) {
        context.add_audit("Publication skipped (no events)");
        return;
    }

    std::vector<ports::OutputPublisher*> outputs;
    for (auto* output : registry_.outputs()) {
        if (output->enabled()) outputs.push_back(output);
    }
    if (outputs.empty()) {
        kLog.warn("No output adapters are enabled; {} event(s) were not delivered", context.events.size());
        context.add_audit("Publication skipped (no outputs enabled)");
        return;
    }

    std::vector<std::string> required_failures;
    std::size_t delivered = 0;

    for (const auto& event : context.events) {
        for (auto* output : outputs) {
            PublishReport report{.output = output->name(),
                                 .event_type = event.event_type,
                                 .ok = false,
                                 .required = output->required(),
                                 .error = {}};
            try {
                output->publish(event, context.correlation());
                report.ok = true;
                ++delivered;
            } catch (const std::exception& e) {
                report.error = e.what();
                metrics_.record_publish_failure(output->name());
                if (report.required) {
                    kLog.error("Required output {} failed: {}", output->name(), e.what());
                    required_failures.push_back(std::format("{}: {}", output->name(), e.what()));
                } else {
                    kLog.warn("Optional output {} failed: {}", output->name(), e.what());
                }
            }
            context.publish_reports.push_back(std::move(report));
        }
    }

    context.add_audit(std::format("Published {} of {} deliveries ({} events x {} outputs)", delivered,
                                  context.events.size() * outputs.size(), context.events.size(),
                                  outputs.size()));

    if (!required_failures.empty()) {
        throw PipelineError(ErrorKind::PublishFailed, "Delivery failed for " + join(required_failures, "; "));
    }
}

void PipelineEngine::publish_error_event(PipelineContext& context) const {
    const PipelineError& error = *context.error;

    nlohmann::ordered_json payload;
    payload["error"] = error.what();
    payload["errorKind"] = to_string(error.kind());
    if (!context.intent.empty()) payload["intent"] = context.intent;
    if (context.header) {
        payload["messageType"] = context.header->message_type;
        payload["messageSubType"] = context.header->message_sub_type;
        payload["sequenceNumber"] = context.header->sequence_number;
    }
    payload["originalXml"] = pretty_print_xml(context.raw_input(), context.document.get());

    domain::FlightEvent event{
        .event_id = util::random_uuid(),
        .trace_id = context.correlation().trace_id,
        .event_type = domain::kPipelineErrorEventType,
        .business_key = context.events.empty() ? "UNKNOWN" : context.events.front().business_key,
        .event_time = util::Clock::now(),
        .payload = std::move(payload),
    };

    std::size_t delivered = 0;
    for (auto* output : registry_.outputs()) {
        if (!output->enabled()) continue;
        PublishReport report{.output = output->name(),
                             .event_type = event.event_type,
                             .ok = false,
                             .required = output->required(),
                             .error = {}};
        try {
            output->publish(event, context.correlation());
            report.ok = true;
            ++delivered;
        } catch (const std::exception& e) {
            report.error = e.what();
            metrics_.record_publish_failure(output->name());
            kLog.error("Could not publish PIPELINE_ERROR event to {}: {}", output->name(), e.what());
        }
        context.publish_reports.push_back(std::move(report));
    }

    context.error_event_delivered = delivered > 0;
    context.error_event = std::move(event);
    context.add_audit(std::format("PIPELINE_ERROR event delivered to {} output(s)", delivered));
}

}  // namespace spinneret::pipeline
