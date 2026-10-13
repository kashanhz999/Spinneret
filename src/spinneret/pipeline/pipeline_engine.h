#pragma once

#include <vector>

#include "spinneret/metrics/metrics.h"
#include "spinneret/pipeline/pipeline_context.h"
#include "spinneret/pipeline/stages.h"
#include "spinneret/pipeline/transformers.h"
#include "spinneret/registry/adapter_registry.h"

namespace spinneret::pipeline {

/// When a failed message should produce a PIPELINE_ERROR event.
enum class ErrorEventPolicy {
    Always,         ///< Every failure (used when the message is being given up on).
    PermanentOnly,  ///< Only failures that would recur on retry.
    Never,
};

struct IngestOptions {
    bool publish = true;  ///< false = dry run: run all stages, publish nothing.
    ErrorEventPolicy error_events = ErrorEventPolicy::PermanentOnly;
};

/// Orchestrates the pipeline:
///
///  1. Header       – HeaderExtractor
///  2. Intent       – IntentResolver
///  3. Parse        – PayloadExtractor (XML → JSON)
///  4. Transform    – TransformerRegistry
///  5. Correlate    – trace ID bound to every log line of this message
///     Publish      – fan-out to every enabled output
///  6. Audit        – the full journey is logged
///
/// ingest() never throws. On failure `context.error` is set, metrics are
/// updated and, according to the policy, a PIPELINE_ERROR event is published.
class PipelineEngine {
public:
    PipelineEngine(TransformerRegistry transformers, const registry::AdapterRegistry& registry,
                   metrics::Metrics& metrics);

    void ingest(PipelineContext& context, const IngestOptions& options = {}) const;

    /// Names of outputs that would receive events right now.
    std::vector<std::string> enabled_outputs() const;

private:
    void run_stages(PipelineContext& context, const IngestOptions& options) const;
    void publish_events(PipelineContext& context) const;
    void publish_error_event(PipelineContext& context) const;

    HeaderExtractor header_extractor_;
    IntentResolver intent_resolver_;
    PayloadExtractor payload_extractor_;
    TransformerRegistry transformers_;
    const registry::AdapterRegistry& registry_;
    metrics::Metrics& metrics_;
};

}  // namespace spinneret::pipeline
