#include "spinneret/pipeline/pipeline_context.h"

#include <format>

namespace spinneret::pipeline {

PipelineContext::PipelineContext(std::string raw_input, domain::CorrelationMetadata correlation)
    : raw_input_(std::move(raw_input)),
      correlation_(std::move(correlation)),
      started_(std::chrono::steady_clock::now()) {}

void PipelineContext::add_audit(std::string_view step) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_);
    audit_log_.push_back(std::format("{} [{}ms]", step, elapsed.count()));
}

}  // namespace spinneret::pipeline
