// End-to-end: the whole application on an ephemeral port, delivering to a
// real local webhook server.

#include <atomic>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>

#include "spinneret/app/application.h"
#include "test_support.h"

using namespace spinneret;
using json = nlohmann::json;

namespace {

struct Received {
    json body;
    std::string trace_header;
};

class Webhook {
public:
    Webhook() {
        server_.Post("/hook", [this](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard lock(mutex_);
            received_.push_back({json::parse(req.body), req.get_header_value("X-Trace-Id")});
            res.status = status.load();
        });
        port_ = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }
    ~Webhook() {
        server_.stop();
        thread_.join();
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/hook"; }
    std::vector<Received> received() {
        std::lock_guard lock(mutex_);
        return received_;
    }

    std::atomic<int> status{200};

private:
    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
    std::mutex mutex_;
    std::vector<Received> received_;
};

class HttpApi : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::copy(SPINNERET_TEMPLATES_DIR, dir_.path() / "templates");

        security::ApiKeyService keys(dir_.path() / "api-keys.json");
        keys.load();
        key_ = keys.create("test");

        config::ConfigService config(dir_.path());
        config.load();
        config.update({{"adapters.output.rest.enabled", true}, {"adapters.output.rest.url", webhook_.url()}});

        app_ = std::make_unique<app::Application>(
            app::AppOptions{.config_dir = dir_.path(), .server = {.host = "127.0.0.1", .port = 0}});
        app_->start();
        client_ = std::make_unique<httplib::Client>("127.0.0.1", app_->http_port());
    }

    void TearDown() override {
        client_.reset();
        app_.reset();
    }

    httplib::Headers auth(httplib::Headers extra = {}) const {
        extra.emplace("X-API-Key", key_);
        return extra;
    }

    httplib::Result ingest(const std::string& xml, httplib::Headers headers) {
        return client_->Post("/api/ingest", headers, xml, "application/xml");
    }

    httplib::Result put_config(const json& body) {
        return client_->Put("/api/admin/config", auth(), body.dump(), "application/json");
    }

    test::TempDir dir_;
    Webhook webhook_;
    std::string key_;
    std::unique_ptr<app::Application> app_;
    std::unique_ptr<httplib::Client> client_;
};

}  // namespace

TEST_F(HttpApi, RejectsMissingOrWrongKey) {
    EXPECT_EQ(ingest(test::fltu_xml(), {})->status, 401);
    EXPECT_EQ(ingest(test::fltu_xml(), {{"X-API-Key", "spn_wrong"}})->status, 401);
    EXPECT_EQ(client_->Post("/api/test/ingest", test::fltu_xml(), "application/xml")->status, 401);
    EXPECT_EQ(client_->Get("/api/admin/config")->status, 401);
    EXPECT_TRUE(webhook_.received().empty());
}

TEST_F(HttpApi, IngestDeliversCanonicalEventToWebhook) {
    auto res = ingest(test::read_sample("FLTU/FGAT/Flight Gate Update FGAT.xml"), auth());
    ASSERT_EQ(res->status, 200) << res->body;
    auto body = json::parse(res->body);
    EXPECT_EQ(body["status"], "PROCESSED");
    EXPECT_EQ(body["intent"], "FLIGHT_GATE_UPDATE");
    EXPECT_EQ(body["businessKey"], "I51569_2024-05-26");

    auto received = webhook_.received();
    ASSERT_EQ(received.size(), 1u);
    const auto& event = received.front().body;
    EXPECT_EQ(event["eventType"], "FLIGHT_GATE_UPDATE");
    EXPECT_EQ(received.front().trace_header, body["traceId"].get<std::string>());
    EXPECT_EQ(event["payloadJson"]["YIAPL_FlightLegNotifRQ"]["YIAPL_Header"]["MessageType"]["__text"], "FLTU");
}

TEST_F(HttpApi, AdoptsUpstreamTraceId) {
    auto res = ingest(test::fltu_xml(), auth({{"X-Trace-Id", "aodb-trace-77"}}));
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("X-Trace-Id"), "aodb-trace-77");
    EXPECT_EQ(webhook_.received().front().trace_header, "aodb-trace-77");
}

TEST_F(HttpApi, UnknownTypeIs422AndPublishesErrorEvent) {
    auto res = ingest(test::fltu_xml("ZZZZ"), auth());
    ASSERT_EQ(res->status, 422) << res->body;
    EXPECT_EQ(json::parse(res->body)["errorKind"], "UNKNOWN_MESSAGE_TYPE");
    auto received = webhook_.received();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received.front().body["eventType"], "PIPELINE_ERROR");
}

TEST_F(HttpApi, MalformedXmlIs400) {
    auto res = ingest("<root>unclosed", auth());
    ASSERT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body)["errorKind"], "INVALID_XML");
}

TEST_F(HttpApi, OptionalWebhookFailureIsReportedButMessageSucceeds) {
    webhook_.status = 500;
    auto res = ingest(test::fltu_xml(), auth());
    ASSERT_EQ(res->status, 200);
    auto deliveries = json::parse(res->body)["deliveries"];
    ASSERT_EQ(deliveries.size(), 1u);
    EXPECT_FALSE(deliveries[0]["ok"]);
    EXPECT_NE(deliveries[0]["error"].get<std::string>().find("HTTP 500"), std::string::npos);
}

TEST_F(HttpApi, RequiredWebhookFailureIs502) {
    ASSERT_EQ(put_config({{"adapters.output.rest.required", true}})->status, 200);
    webhook_.status = 503;
    auto res = ingest(test::fltu_xml(), auth());
    ASSERT_EQ(res->status, 502) << res->body;
    EXPECT_EQ(json::parse(res->body)["errorKind"], "PUBLISH_FAILED");
}

TEST_F(HttpApi, DiagnosticIsDryRunByDefault) {
    auto res = client_->Post("/api/test/ingest", auth(), test::fltu_xml(), "application/xml");
    ASSERT_EQ(res->status, 200) << res->body;
    auto report = json::parse(res->body);
    EXPECT_EQ(report["status"], "SUCCESS");
    EXPECT_FALSE(report["published"]);
    EXPECT_EQ(report["wouldPublishTo"], json::array({"RestOutput"}));
    EXPECT_TRUE(report.contains("parsedInput"));
    EXPECT_FALSE(report["auditLog"].empty());
    EXPECT_TRUE(webhook_.received().empty());

    res = client_->Post("/api/test/ingest?publish=true", auth(), test::fltu_xml(), "application/xml");
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(webhook_.received().size(), 1u);
}

TEST_F(HttpApi, ConfigValidationAndMasking) {
    auto bad = put_config({{"adapters.output.rest.enable", true}});
    EXPECT_EQ(bad->status, 400);
    EXPECT_NE(bad->body.find("Unknown configuration key"), std::string::npos);

    ASSERT_EQ(put_config({{"ibm.mq.password", "s3cret"}})->status, 200);
    auto res = client_->Get("/api/admin/config", auth());
    ASSERT_EQ(res->status, 200);
    auto config = json::parse(res->body);
    EXPECT_EQ(config["ibm.mq.password"], "********");
    EXPECT_EQ(res->body.find("s3cret"), std::string::npos);
}

TEST_F(HttpApi, AdapterLifecycle) {
    auto start_mq = client_->Post("/api/admin/adapters/IbmMqInput/start", auth(), "", "application/json");
    EXPECT_EQ(start_mq->status, 409) << "disabled adapters cannot be started";
    EXPECT_EQ(client_->Post("/api/admin/adapters/Nope/start", auth(), "", "application/json")->status, 404);

    ASSERT_EQ(client_->Post("/api/admin/adapters/RestInput/stop", auth(), "", "application/json")->status, 200);
    EXPECT_EQ(ingest(test::fltu_xml(), auth())->status, 503);
    ASSERT_EQ(client_->Post("/api/admin/adapters/RestInput/start", auth(), "", "application/json")->status, 200);
    EXPECT_EQ(ingest(test::fltu_xml(), auth())->status, 200);

    auto inventory = json::parse(client_->Get("/api/admin/adapters", auth())->body);
    EXPECT_EQ(inventory["inputs"].size(), 2u);
    EXPECT_EQ(inventory["outputs"].size(), 2u);
}

TEST_F(HttpApi, DisablingRestInputViaConfigRestartsIt) {
    auto res = put_config({{"adapters.input.rest.enabled", false}});
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(json::parse(res->body)["restarted"], json::array({"RestInput"}));
    EXPECT_EQ(ingest(test::fltu_xml(), auth())->status, 503);
}

TEST_F(HttpApi, TemplatesAndRestore) {
    auto list = client_->Get("/api/admin/config/templates", auth());
    EXPECT_EQ(json::parse(list->body), json::array({"development", "production", "rest-only"}));
    EXPECT_EQ(client_->Post("/api/admin/config/template/bad.name", auth(), "", "application/json")->status, 400);
    EXPECT_EQ(client_->Post("/api/admin/config/template/missing", auth(), "", "application/json")->status, 404);

    ASSERT_EQ(client_->Post("/api/admin/config/template/production", auth(), "", "application/json")->status, 200);
    auto config = json::parse(client_->Get("/api/admin/config", auth())->body);
    EXPECT_EQ(config["ibm.mq.queueManager"], "QM_PROD");

    ASSERT_EQ(client_->Post("/api/admin/config/restore", auth(), "", "application/json")->status, 200);
    config = json::parse(client_->Get("/api/admin/config", auth())->body);
    EXPECT_EQ(config["ibm.mq.queueManager"], "QM1");
}

TEST_F(HttpApi, OperationalEndpoints) {
    auto health = client_->Get("/health");
    ASSERT_EQ(health->status, 200) << health->body;
    EXPECT_EQ(json::parse(health->body)["status"], "UP");
    EXPECT_EQ(client_->Get("/health/live")->status, 200);

    ingest(test::fltu_xml(), auth());
    auto metrics = client_->Get("/metrics");
    ASSERT_EQ(metrics->status, 200);
    EXPECT_NE(metrics->body.find("spinneret_pipeline_messages_total{outcome=\"success\"} 1"), std::string::npos)
        << metrics->body;

    auto spec = json::parse(client_->Get("/openapi.json")->body);
    EXPECT_EQ(spec["openapi"], "3.0.3");
    EXPECT_TRUE(spec["paths"].contains("/api/ingest"));

    EXPECT_EQ(client_->Get("/docs")->status, 200);
    EXPECT_EQ(client_->Get("/nope")->status, 404);
}

TEST_F(HttpApi, HealthIsDegradedWhenAnEnabledOutputFails) {
    webhook_.status = 500;
    ingest(test::fltu_xml(), auth());
    auto health = client_->Get("/health");
    EXPECT_EQ(health->status, 503);
    EXPECT_EQ(json::parse(health->body)["status"], "DEGRADED");
}
