#include "spinneret/metrics/metrics.h"

#include <format>

#include "spinneret/version.h"

namespace spinneret::metrics {

void Metrics::observe_latency(double seconds) {
    for (std::size_t i = 0; i < kLatencyBuckets.size(); ++i) {
        if (seconds <= kLatencyBuckets[i]) ++latency_buckets_[i];
    }
    ++latency_count_;
    latency_sum_ += seconds;
}

void Metrics::record_success(double latency_seconds) {
    std::lock_guard lock(mutex_);
    ++successes_;
    observe_latency(latency_seconds);
}

void Metrics::record_failure(pipeline::ErrorKind kind, double latency_seconds) {
    std::lock_guard lock(mutex_);
    ++failures_;
    ++errors_by_kind_[std::string(pipeline::to_string(kind))];
    observe_latency(latency_seconds);
}

void Metrics::record_publish_failure(std::string_view output) {
    std::lock_guard lock(mutex_);
    ++publish_failures_[std::string(output)];
}

void Metrics::record_received(std::string_view adapter) {
    std::lock_guard lock(mutex_);
    ++received_[std::string(adapter)];
}

std::uint64_t Metrics::successes() const {
    std::lock_guard lock(mutex_);
    return successes_;
}

std::uint64_t Metrics::failures() const {
    std::lock_guard lock(mutex_);
    return failures_;
}

std::string Metrics::render_prometheus() const {
    std::lock_guard lock(mutex_);
    std::string out;

    out += "# HELP spinneret_info Build information.\n# TYPE spinneret_info gauge\n";
    out += std::format("spinneret_info{{version=\"{}\",ibmmq=\"{}\"}} 1\n", kVersion, kWithIbmMq);

    out += "# HELP spinneret_messages_received_total Messages received, by input adapter.\n"
           "# TYPE spinneret_messages_received_total counter\n";
    for (const auto& [adapter, count] : received_) {
        out += std::format("spinneret_messages_received_total{{adapter=\"{}\"}} {}\n", adapter, count);
    }

    out += "# HELP spinneret_pipeline_messages_total Messages processed, by outcome.\n"
           "# TYPE spinneret_pipeline_messages_total counter\n";
    out += std::format("spinneret_pipeline_messages_total{{outcome=\"success\"}} {}\n", successes_);
    out += std::format("spinneret_pipeline_messages_total{{outcome=\"failed\"}} {}\n", failures_);

    out += "# HELP spinneret_pipeline_errors_total Pipeline failures, by error kind.\n"
           "# TYPE spinneret_pipeline_errors_total counter\n";
    for (const auto& [kind, count] : errors_by_kind_) {
        out += std::format("spinneret_pipeline_errors_total{{kind=\"{}\"}} {}\n", kind, count);
    }

    out += "# HELP spinneret_publish_failures_total Failed deliveries, by output adapter.\n"
           "# TYPE spinneret_publish_failures_total counter\n";
    for (const auto& [output, count] : publish_failures_) {
        out += std::format("spinneret_publish_failures_total{{output=\"{}\"}} {}\n", output, count);
    }

    out += "# HELP spinneret_pipeline_latency_seconds End-to-end pipeline latency.\n"
           "# TYPE spinneret_pipeline_latency_seconds histogram\n";
    for (std::size_t i = 0; i < kLatencyBuckets.size(); ++i) {
        out += std::format("spinneret_pipeline_latency_seconds_bucket{{le=\"{}\"}} {}\n", kLatencyBuckets[i],
                           latency_buckets_[i]);
    }
    out += std::format("spinneret_pipeline_latency_seconds_bucket{{le=\"+Inf\"}} {}\n", latency_count_);
    out += std::format("spinneret_pipeline_latency_seconds_sum {}\n", latency_sum_);
    out += std::format("spinneret_pipeline_latency_seconds_count {}\n", latency_count_);

    return out;
}

}  // namespace spinneret::metrics
