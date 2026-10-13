#include "spinneret/app/application.h"

#include "spinneret/http/admin_routes.h"
#include "spinneret/http/ops_routes.h"
#include "spinneret/util/logging.h"
#include "spinneret/version.h"

namespace spinneret::app {

namespace {
const util::Logger kLog{"app"};
}

Application::Application(AppOptions options, std::unique_ptr<adapters::mq::MqSessionFactory> mq_factory)
    : options_(std::move(options)),
      config_(options_.config_dir),
      templates_(config_),
      keys_(options_.config_dir / "api-keys.json"),
      mq_factory_(mq_factory ? std::move(mq_factory) : adapters::mq::make_default_factory()),
      engine_(pipeline::TransformerRegistry(pipeline::default_transformers()), registry_, metrics_),
      rest_output_(config_),
      mq_output_(config_, *mq_factory_),
      rest_input_(engine_, config_, keys_, metrics_),
      mq_input_(engine_, config_, *mq_factory_, metrics_),
      restart_(registry_),
      server_(options_.server) {
    registry_.register_input(rest_input_);
    registry_.register_input(mq_input_);
    registry_.register_output(rest_output_);
    registry_.register_output(mq_output_);
}

Application::~Application() { stop(); }

void Application::start() {
    kLog.info("{} {} starting (config: {}, IBM MQ support: {})", kName, kVersion,
              std::filesystem::absolute(options_.config_dir).string(), kWithIbmMq ? "yes" : "no");

    config_.load();
    keys_.load();
    if (auto key = keys_.ensure_bootstrap_key()) {
        kLog.warn("==================================================================");
        kLog.warn("No API keys existed, so one was created. It is shown only once:");
        kLog.warn("  {}", *key);
        kLog.warn("Create named keys with `spinneret keys create <name>` and revoke this one.");
        kLog.warn("==================================================================");
    }

    rest_input_.register_routes(server_.server());
    http::register_admin_routes(server_.server(), {.config = config_,
                                                   .templates = templates_,
                                                   .registry = registry_,
                                                   .restart = restart_,
                                                   .keys = keys_});
    http::register_ops_routes(server_.server(), registry_, metrics_);

    for (auto* input : registry_.inputs()) {
        if (input->enabled()) input->start();
    }

    auto outputs = engine_.enabled_outputs();
    if (outputs.empty()) {
        kLog.warn("No output adapters are enabled; processed events will not be delivered anywhere");
    } else {
        for (const auto& name : outputs) kLog.info("Output enabled: {}", name);
    }

    server_.start();
    started_ = true;
    kLog.info("{} ready on port {}", kName, server_.port());
}

void Application::stop() {
    if (!started_) return;
    started_ = false;
    kLog.info("Shutting down");
    server_.stop();
    for (auto* input : registry_.inputs()) input->stop();
}

}  // namespace spinneret::app
