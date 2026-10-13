#include "spinneret/domain/correlation_metadata.h"

#include "spinneret/util/random.h"
#include "spinneret/util/strings.h"

namespace spinneret::domain {

bool is_safe_trace_id(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '.' || c == ':';
        if (!ok) return false;
    }
    return true;
}

CorrelationMetadata CorrelationMetadata::create(std::optional<std::string_view> upstream_trace_id,
                                                std::string originator) {
    std::string trace_id;
    if (upstream_trace_id) {
        auto candidate = util::trim(*upstream_trace_id);
        if (is_safe_trace_id(candidate)) trace_id = std::string(candidate);
    }
    if (trace_id.empty()) trace_id = util::random_uuid();

    return CorrelationMetadata{
        .trace_id = std::move(trace_id),
        .span_id = util::random_uuid(),
        .correlation_id = {},
        .originator = std::move(originator),
        .ingress_time = util::Clock::now(),
    };
}

}  // namespace spinneret::domain
