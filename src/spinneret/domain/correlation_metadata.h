#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "spinneret/util/time.h"

namespace spinneret::domain {

/// Tracing identity carried by one message through the pipeline.
struct CorrelationMetadata {
    std::string trace_id;        ///< End-to-end trace; inherited from upstream when supplied.
    std::string span_id;         ///< Unique to this processing attempt; used as the event ID.
    std::string correlation_id;  ///< Optional upstream correlation ID.
    std::string originator;      ///< Ingress channel, e.g. "REST_ADAPTER" or "IBM_MQ".
    util::Clock::time_point ingress_time;

    /// Creates metadata for a newly received message. An upstream trace ID is
    /// adopted only if it is a safe identifier (alphanumerics plus `-_.:`, at
    /// most 128 characters); otherwise a fresh UUID is generated.
    static CorrelationMetadata create(std::optional<std::string_view> upstream_trace_id,
                                      std::string originator);
};

/// True when `id` is acceptable as an externally supplied trace ID.
bool is_safe_trace_id(std::string_view id);

}  // namespace spinneret::domain
