// Retry, backout and error-event behaviour of the IBM MQ adapters, using fake
// sessions so it runs without a queue manager or the MQ SDK.

#include <thread>

#include <gtest/gtest.h>

#include "spinneret/adapters/in/ibmmq_input_adapter.h"
#include "spinneret/adapters/out/ibmmq_output_adapter.h"
#include "test_support.h"

using namespace spinneret;
using adapters::IbmMqInputAdapter;
using Disposition = IbmMqInputAdapter::Disposition;

namespace {

class FakeInputSession final : public adapters::mq::MqInputSession {
public:
    std::optional<adapters::mq::MqMessage> get(std::chrono::milliseconds) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return std::nullopt;
    }
    void commit() override { ++commits; }
    void rollback() override { ++rollbacks; }
    void move_to_backout() override {
        if (fail_backout) throw adapters::mq::MqError("MQPUT1", 2, 2053);  // MQRC_Q_FULL
        ++moved;
    }
    int backout_threshold() const override { return threshold; }
    std::string backout_queue() const override { return boq; }

    int threshold = 3;
    std::string boq;
    bool fail_backout = false;
    int commits = 0;
    int rollbacks = 0;
    int moved = 0;
};

class FakeOutputSession final : public adapters::mq::MqOutputSession {
public:
    explicit FakeOutputSession(int& puts, int& failures_left) : puts_(puts), failures_left_(failures_left) {}
    void put(const std::string&, std::string_view, const std::vector<std::pair<std::string, std::string>>& props) override {
        if (failures_left_ > 0) {
            --failures_left_;
            throw adapters::mq::MqError("MQPUT", 2, adapters::mq::reason::kConnectionBroken);
        }
        last_props = props;
        ++puts_;
    }
    std::vector<std::pair<std::string, std::string>> last_props;

private:
    int& puts_;
    int& failures_left_;
};

class FakeFactory final : public adapters::mq::MqSessionFactory {
public:
    bool available() const override { return available_; }
    std::unique_ptr<adapters::mq::MqInputSession> open_input(const adapters::mq::MqConnectionSettings&,
                                                             const adapters::mq::MqInputOptions&) override {
        return std::make_unique<FakeInputSession>();
    }
    std::unique_ptr<adapters::mq::MqOutputSession> open_output(const adapters::mq::MqConnectionSettings&) override {
        ++connects;
        return std::make_unique<FakeOutputSession>(puts, failures_left);
    }

    bool available_ = true;
    int connects = 0;
    int puts = 0;
    int failures_left = 0;
};

struct MqFixture {
    test::TempDir dir;
    config::ConfigService config{dir.path()};
    test::EngineFixture fx;
    FakeFactory factory;
    test::FakeOutput output{"Out", /*required=*/true};
    IbmMqInputAdapter adapter{fx.engine, config, factory, fx.metrics};
    FakeInputSession session;

    MqFixture() {
        config.load();
        fx.registry.register_output(output);
    }

    IbmMqInputAdapter::Outcome deliver(const std::string& body, int backout_count = 0) {
        return adapter.handle_message(session, {.body = body, .backout_count = backout_count, .trace_id = "t-1"});
    }

    int error_events() const {
        int n = 0;
        for (const auto& e : output.events()) n += e.event_type == "PIPELINE_ERROR";
        return n;
    }
};

}  // namespace

TEST(MqInput, SuccessCommits) {
    MqFixture f;
    auto outcome = f.deliver(test::fltu_xml());
    EXPECT_EQ(outcome.disposition, Disposition::Committed);
    EXPECT_EQ(f.session.commits, 1);
    ASSERT_EQ(f.output.events().size(), 1u);
    EXPECT_EQ(f.output.events().front().trace_id, "t-1") << "AODB_TRACE_ID is adopted";
}

TEST(MqInput, PermanentFailureGoesToBackoutQueueImmediately) {
    MqFixture f;
    f.session.boq = "AODB.FLIGHT.UPDATE.BACKOUT";
    auto outcome = f.deliver("<broken");
    EXPECT_EQ(outcome.disposition, Disposition::MovedToBackout);
    EXPECT_EQ(f.session.moved, 1);
    EXPECT_EQ(f.session.commits, 1);
    EXPECT_EQ(f.session.rollbacks, 0);
    EXPECT_EQ(f.error_events(), 1);
}

TEST(MqInput, PermanentFailureWithoutBackoutQueueIsDiscardedOnceReported) {
    MqFixture f;
    auto outcome = f.deliver(test::fltu_xml("ZZZZ"));
    EXPECT_EQ(outcome.disposition, Disposition::Discarded);
    EXPECT_EQ(f.session.commits, 1);
    EXPECT_EQ(f.error_events(), 1) << "the PIPELINE_ERROR event preserves the XML";
}

TEST(MqInput, TransientFailureRollsBackWithoutErrorEvent) {
    MqFixture f;
    f.output.fail_ = true;
    auto outcome = f.deliver(test::fltu_xml(), /*backout_count=*/0);
    EXPECT_EQ(outcome.disposition, Disposition::RolledBack);
    EXPECT_GT(outcome.retry_delay.count(), 0);
    EXPECT_EQ(f.session.rollbacks, 1);
    EXPECT_EQ(f.session.commits, 0);
}

TEST(MqInput, TransientFailureOnLastAttemptMovesToBackout) {
    MqFixture f;
    test::FakeOutput webhook{"Webhook", /*required=*/false};
    f.fx.registry.register_output(webhook);
    f.session.boq = "BOQ";
    f.output.fail_ = true;

    auto outcome = f.deliver(test::fltu_xml(), /*backout_count=*/2);  // attempt 3 of 3
    EXPECT_EQ(outcome.disposition, Disposition::MovedToBackout);
    EXPECT_EQ(f.session.moved, 1);
    bool error_event = false;
    for (const auto& e : webhook.events()) error_event |= e.event_type == "PIPELINE_ERROR";
    EXPECT_TRUE(error_event) << "the last attempt reports the failure";
}

TEST(MqInput, NothingAcceptsTheFailureSoItRetriesUntilTheLastAttempt) {
    MqFixture f;
    f.output.fail_ = true;  // error event cannot be delivered either

    EXPECT_EQ(f.deliver("<broken", 0).disposition, Disposition::RolledBack);
    EXPECT_EQ(f.deliver("<broken", 2).disposition, Disposition::Discarded);
    EXPECT_EQ(f.session.rollbacks, 1);
    EXPECT_EQ(f.session.commits, 1);
}

TEST(MqInput, BackoutQueueFailureFallsBackToErrorEvent) {
    MqFixture f;
    f.session.boq = "BOQ";
    f.session.fail_backout = true;
    auto outcome = f.deliver("<broken");
    EXPECT_EQ(outcome.disposition, Disposition::Discarded);
    EXPECT_EQ(f.error_events(), 1);
}

TEST(MqInput, StartFailsClearlyWithoutMqSupport) {
    MqFixture f;
    f.factory.available_ = false;
    f.config.update({{"adapters.input.ibmmq.enabled", true}});
    EXPECT_EQ(f.adapter.start(), ports::StartResult::Failed);
    EXPECT_NE(f.adapter.status_detail().find("without IBM MQ"), std::string::npos);
}

TEST(MqInput, StartRespectsEnabledFlag) {
    MqFixture f;
    EXPECT_EQ(f.adapter.start(), ports::StartResult::Disabled);
    f.config.update({{"adapters.input.ibmmq.enabled", true}});
    EXPECT_EQ(f.adapter.start(), ports::StartResult::Started);
    EXPECT_EQ(f.adapter.start(), ports::StartResult::AlreadyRunning);
    f.adapter.stop();
    EXPECT_FALSE(f.adapter.running());
}

TEST(MqOutput, ReconnectsOnceAfterConnectionLoss) {
    test::TempDir dir;
    config::ConfigService config(dir.path());
    config.load();
    FakeFactory factory;
    config.update({{"adapters.output.ibmmq.enabled", true}});
    adapters::IbmMqOutputAdapter output(config, factory);

    domain::FlightEvent event{.event_type = "X", .payload = nlohmann::ordered_json::object()};
    auto meta = domain::CorrelationMetadata::create("trace-9", "TEST");

    factory.failures_left = 1;
    output.publish(event, meta);
    EXPECT_EQ(factory.connects, 2);
    EXPECT_EQ(factory.puts, 1);
    EXPECT_TRUE(output.healthy());

    factory.failures_left = 2;
    EXPECT_THROW(output.publish(event, meta), adapters::mq::MqError);
    EXPECT_FALSE(output.healthy());
}
