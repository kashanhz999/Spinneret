#include "spinneret/adapters/in/rest_input_adapter.h"

#include <format>

#include "spinneret/http/http_util.h"
#include "spinneret/pipeline/diagnostics.h"
#include "spinneret/util/logging.h"

namespace spinneret::adapters {

namespace {

const util::Logger kLog{"adapter.rest.in"};

nlohmann::ordered_json deliveries_json(const pipeline::PipelineContext& context) {
    auto list = nlohmann::ordered_json::array();
    for (const auto& r : context.publish_reports) {
        nlohmann::ordered_json d{{"output", r.output}, {"eventType", r.event_type}, {"ok", r.ok}};
        if (!r.ok) d["error"] = r.error;
        list.push_back(std::move(d));
    }
    return list;
}

}  // namespace

RestInputAdapter::RestInputAdapter(const pipeline::PipelineEngine& engine, const config::ConfigService& config,
                                   const security::ApiKeyService& keys, metrics::Metrics& metrics)
    : engine_(engine), config_(config), keys_(keys), metrics_(metrics) {}

bool RestInputAdapter::enabled() const { return config_.get_bool("adapters.input.rest.enabled"); }

ports::StartResult RestInputAdapter::start() {
    if (!enabled()) {
        running_ = false;
        kLog.info("RestInput is disabled in the configuration");
        return ports::StartResult::Disabled;
    }
    if (running_.exchange(true)) return ports::StartResult::AlreadyRunning;
    kLog.info("RestInput STARTED");
    return ports::StartResult::Started;
}

void RestInputAdapter::stop() {
    if (running_.exchange(false)) kLog.info("RestInput STOPPED");
}

std::string RestInputAdapter::status_detail() const {
    return std::format("{} message(s) received", processed_.load());
}

void RestInputAdapter::register_routes(httplib::Server& server) {
    server.Post("/api/ingest", [this](const httplib::Request& req, httplib::Response& res) {
        handle_ingest(req, res);
    });
    server.Post("/api/test/ingest", [this](const httplib::Request& req, httplib::Response& res) {
        handle_diagnostic(req, res);
    });
}

void RestInputAdapter::handle_ingest(const httplib::Request& req, httplib::Response& res) {
    if (!running_) {
        http::send_error(res, 503, "REST input adapter is stopped or disabled");
        return;
    }
    if (!http::authorize(req, res, keys_)) return;

    metrics_.record_received(name());
    ++processed_;

    auto correlation = domain::CorrelationMetadata::create(http::header(req, http::kTraceHeader), "REST_ADAPTER");
    pipeline::PipelineContext context(req.body, std::move(correlation));
    engine_.ingest(context, {.publish = true, .error_events = pipeline::ErrorEventPolicy::PermanentOnly});

    res.set_header(http::kTraceHeader, context.correlation().trace_id);

    if (context.error) {
        http::send_json(res, http::status_for(context.error->kind()),
                        {
                            {"status", "FAILED"},
                            {"errorKind", pipeline::to_string(context.error->kind())},
                            {"error", context.error->what()},
                            {"traceId", context.correlation().trace_id},
                            {"deliveries", deliveries_json(context)},
                        });
        return;
    }

    http::send_json(res, 200,
                    {
                        {"status", "PROCESSED"},
                        {"traceId", context.correlation().trace_id},
                        {"intent", context.intent},
                        {"businessKey", context.events.empty() ? "" : context.events.front().business_key},
                        {"events", context.events.size()},
                        {"deliveries", deliveries_json(context)},
                        {"timestamp", util::iso8601_utc(util::Clock::now())},
                    });
}

void RestInputAdapter::handle_diagnostic(const httplib::Request& req, httplib::Response& res) {
    if (!http::authorize(req, res, keys_)) return;

    const bool publish = req.get_param_value("publish") == "true";
    auto correlation = domain::CorrelationMetadata::create(http::header(req, http::kTraceHeader), "DIAGNOSTIC");
    pipeline::PipelineContext context(req.body, std::move(correlation));
    engine_.ingest(context, {.publish = publish,
                             .error_events = publish ? pipeline::ErrorEventPolicy::PermanentOnly
                                                     : pipeline::ErrorEventPolicy::Never});

    res.set_header(http::kTraceHeader, context.correlation().trace_id);
    auto report = pipeline::diagnostic_report(context, publish);
    if (!publish) report["wouldPublishTo"] = engine_.enabled_outputs();
    http::send_json(res, context.error ? http::status_for(context.error->kind()) : 200, report);
}

}  // namespace spinneret::adapters
