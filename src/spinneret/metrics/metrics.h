#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "spinneret/pipeline/pipeline_error.h"

namespace spinneret::metrics {

/// Pipeline counters and latency histogram, rendered in the Prometheus text
/// exposition format at `/metrics`.
class Metrics {
public:
    static constexpr std::array<double, 12> kLatencyBuckets = {
        0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};

    void record_success(double latency_seconds);
    void record_failure(pipeline::ErrorKind kind, double latency_seconds);
    void record_publish_failure(std::string_view output);
    void record_received(std::string_view adapter);

    std::string render_prometheus() const;

    std::uint64_t successes() const;
    std::uint64_t failures() const;

private:
    void observe_latency(double seconds);

    mutable std::mutex mutex_;
    std::uint64_t successes_ = 0;
    std::uint64_t failures_ = 0;
    std::map<std::string, std::uint64_t> errors_by_kind_;
    std::map<std::string, std::uint64_t> publish_failures_;
    std::map<std::string, std::uint64_t> received_;
    std::array<std::uint64_t, kLatencyBuckets.size()> latency_buckets_{};
    std::uint64_t latency_count_ = 0;
    double latency_sum_ = 0;
};

}  // namespace spinneret::metrics
