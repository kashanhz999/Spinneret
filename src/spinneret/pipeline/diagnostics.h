#pragma once

#include <nlohmann/json.hpp>

#include "spinneret/pipeline/pipeline_context.h"

namespace spinneret::pipeline {

/// Detailed execution report: status, header, intent, parsed input, events,
/// delivery results and the audit log. Used by `POST /api/test/ingest` and
/// `spinneret process`.
nlohmann::ordered_json diagnostic_report(const PipelineContext& context, bool published);

}  // namespace spinneret::pipeline
