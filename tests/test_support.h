#pragma once

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "spinneret/metrics/metrics.h"
#include "spinneret/pipeline/pipeline_engine.h"
#include "spinneret/ports/output_publisher.h"
#include "spinneret/registry/adapter_registry.h"

namespace spinneret::test {

/// Temporary directory removed on destruction.
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/// Records published events; can be told to fail.
class FakeOutput final : public ports::OutputPublisher {
public:
    explicit FakeOutput(std::string name = "FakeOutput", bool required = true)
        : name_(std::move(name)), required_(required) {}

    std::string name() const override { return name_; }
    std::string type() const override { return "Fake"; }
    bool enabled() const override { return enabled_; }
    bool required() const override { return required_; }
    bool healthy() const override { return enabled_ && !fail_; }

    void publish(const domain::FlightEvent& event, const domain::CorrelationMetadata&) override {
        std::lock_guard lock(mutex_);
        if (fail_) throw std::runtime_error(name_ + " is down");
        events_.push_back(event);
    }

    std::vector<domain::FlightEvent> events() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

    bool enabled_ = true;
    bool required_;
    bool fail_ = false;

private:
    std::string name_;
    mutable std::mutex mutex_;
    std::vector<domain::FlightEvent> events_;
};

/// A pipeline engine wired to fake outputs.
struct EngineFixture {
    metrics::Metrics metrics;
    registry::AdapterRegistry registry;
    pipeline::PipelineEngine engine{pipeline::TransformerRegistry(pipeline::default_transformers()), registry, metrics};

    pipeline::PipelineContext run(const std::string& xml, const pipeline::IngestOptions& options = {}) const;
};

std::string read_sample(const std::string& relative_path);
std::vector<std::filesystem::path> all_samples();

/// Minimal valid FLTU message for a given subtype.
std::string fltu_xml(const std::string& sub_type = "FGAT", const std::string& message_type = "FLTU");

}  // namespace spinneret::test
