#pragma once

#include <atomic>
#include <cstdint>

#include "spinneret/config/config_service.h"
#include "spinneret/metrics/metrics.h"
#include "spinneret/pipeline/pipeline_engine.h"
#include "spinneret/ports/input_adapter.h"
#include "spinneret/security/api_key_service.h"

namespace httplib {
class Server;
struct Request;
struct Response;
}  // namespace httplib

namespace spinneret::adapters {

/// HTTP ingestion.
///
///  - `POST /api/ingest`       – production endpoint. Runs the full pipeline
///                               synchronously and reports the outcome.
///  - `POST /api/test/ingest`  – diagnostic report. Dry run by default (no
///                               publishing); `?publish=true` delivers for real.
///
/// Both require an API key. Only /api/ingest is affected by start/stop.
class RestInputAdapter final : public ports::InputAdapter {
public:
    RestInputAdapter(const pipeline::PipelineEngine& engine, const config::ConfigService& config,
                     const security::ApiKeyService& keys, metrics::Metrics& metrics);

    void register_routes(httplib::Server& server);

    std::string name() const override { return "RestInput"; }
    std::string type() const override { return "Rest"; }
    ports::StartResult start() override;
    void stop() override;
    bool enabled() const override;
    bool running() const override { return running_; }
    bool healthy() const override { return running_; }
    std::string status_detail() const override;

private:
    void handle_ingest(const httplib::Request& req, httplib::Response& res);
    void handle_diagnostic(const httplib::Request& req, httplib::Response& res);

    const pipeline::PipelineEngine& engine_;
    const config::ConfigService& config_;
    const security::ApiKeyService& keys_;
    metrics::Metrics& metrics_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> processed_{0};
};

}  // namespace spinneret::adapters
