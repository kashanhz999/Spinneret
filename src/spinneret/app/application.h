#pragma once

#include <filesystem>
#include <memory>

#include "spinneret/adapters/in/ibmmq_input_adapter.h"
#include "spinneret/adapters/in/rest_input_adapter.h"
#include "spinneret/adapters/mq/mq_session.h"
#include "spinneret/adapters/out/ibmmq_output_adapter.h"
#include "spinneret/adapters/out/rest_output_adapter.h"
#include "spinneret/config/config_service.h"
#include "spinneret/config/template_service.h"
#include "spinneret/http/http_server.h"
#include "spinneret/lifecycle/smart_restart.h"
#include "spinneret/metrics/metrics.h"
#include "spinneret/pipeline/pipeline_engine.h"
#include "spinneret/registry/adapter_registry.h"
#include "spinneret/security/api_key_service.h"

namespace spinneret::app {

struct AppOptions {
    std::filesystem::path config_dir = "config";
    http::ServerOptions server;
};

/// Wires every component together (the role Spring's container played in
/// SXPM). Members are declared in dependency order, so destruction runs in
/// reverse: the HTTP server and inputs stop before what they use goes away.
class Application {
public:
    /// `mq_factory` defaults to the build's IBM MQ implementation; tests can
    /// pass a fake.
    explicit Application(AppOptions options, std::unique_ptr<adapters::mq::MqSessionFactory> mq_factory = nullptr);
    ~Application();

    /// Loads config and keys, starts enabled inputs and the HTTP server.
    void start();
    void stop();

    int http_port() const { return server_.port(); }
    config::ConfigService& config() { return config_; }
    security::ApiKeyService& keys() { return keys_; }
    metrics::Metrics& metrics() { return metrics_; }
    registry::AdapterRegistry& registry() { return registry_; }

private:
    AppOptions options_;
    config::ConfigService config_;
    config::TemplateService templates_;
    security::ApiKeyService keys_;
    metrics::Metrics metrics_;
    registry::AdapterRegistry registry_;
    std::unique_ptr<adapters::mq::MqSessionFactory> mq_factory_;
    pipeline::PipelineEngine engine_;
    adapters::RestOutputAdapter rest_output_;
    adapters::IbmMqOutputAdapter mq_output_;
    adapters::RestInputAdapter rest_input_;
    adapters::IbmMqInputAdapter mq_input_;
    lifecycle::SmartRestart restart_;
    http::HttpServer server_;
    bool started_ = false;
};

}  // namespace spinneret::app
