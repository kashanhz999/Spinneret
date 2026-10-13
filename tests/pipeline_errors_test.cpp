#include <gtest/gtest.h>

#include "test_support.h"

using namespace spinneret;
using pipeline::ErrorEventPolicy;
using pipeline::ErrorKind;

TEST(PipelineErrors, MalformedXmlPublishesErrorEventWithRawXml) {
    test::EngineFixture fx;
    test::FakeOutput output;
    fx.registry.register_output(output);

    auto ctx = fx.run("<root>unclosed");

    ASSERT_TRUE(ctx.failed());
    EXPECT_EQ(ctx.error->kind(), ErrorKind::InvalidXml);
    auto events = output.events();
    ASSERT_EQ(events.size(), 1u);
    const auto& event = events.front();
    EXPECT_EQ(event.event_type, "PIPELINE_ERROR");
    EXPECT_EQ(event.business_key, "UNKNOWN");
    EXPECT_EQ(event.payload["errorKind"], "INVALID_XML");
    EXPECT_EQ(event.payload["originalXml"], "<root>unclosed");
    EXPECT_TRUE(ctx.error_event_delivered);
    EXPECT_EQ(fx.metrics.failures(), 1u);
}

TEST(PipelineErrors, UnknownTypePublishesErrorEventWithPrettyXml) {
    test::EngineFixture fx;
    test::FakeOutput output;
    fx.registry.register_output(output);

    auto ctx = fx.run(test::fltu_xml("ZZZZ"));

    ASSERT_TRUE(ctx.failed());
    EXPECT_EQ(ctx.error->kind(), ErrorKind::UnknownMessageType);
    EXPECT_TRUE(ctx.events.empty());
    auto events = output.events();
    ASSERT_EQ(events.size(), 1u);
    const auto& payload = events.front().payload;
    EXPECT_EQ(payload["messageSubType"], "ZZZZ");
    std::string xml = payload["originalXml"];
    EXPECT_NE(xml.find("\n  <yiapl:YIAPL_Header>"), std::string::npos) << xml;
}

TEST(PipelineErrors, RequiredOutputFailureIsTransientAndReported) {
    test::EngineFixture fx;
    test::FakeOutput mq("Mq", /*required=*/true);
    test::FakeOutput webhook("Webhook", /*required=*/false);
    fx.registry.register_output(mq);
    fx.registry.register_output(webhook);
    mq.fail_ = true;

    auto ctx = fx.run(test::fltu_xml(), {.publish = true, .error_events = ErrorEventPolicy::PermanentOnly});

    ASSERT_TRUE(ctx.failed());
    EXPECT_EQ(ctx.error->kind(), ErrorKind::PublishFailed);
    EXPECT_FALSE(pipeline::is_permanent(ctx.error->kind()));
    EXPECT_FALSE(ctx.error_event.has_value()) << "transient failures do not emit PIPELINE_ERROR under PermanentOnly";
    EXPECT_EQ(webhook.events().size(), 1u) << "healthy outputs still receive the event";
    ASSERT_EQ(ctx.publish_reports.size(), 2u);
}

TEST(PipelineErrors, OptionalOutputFailureDoesNotFailTheMessage) {
    test::EngineFixture fx;
    test::FakeOutput mq("Mq", true);
    test::FakeOutput webhook("Webhook", false);
    fx.registry.register_output(mq);
    fx.registry.register_output(webhook);
    webhook.fail_ = true;

    auto ctx = fx.run(test::fltu_xml());

    EXPECT_FALSE(ctx.failed());
    EXPECT_EQ(mq.events().size(), 1u);
    bool reported = false;
    for (const auto& r : ctx.publish_reports) reported |= (r.output == "Webhook" && !r.ok);
    EXPECT_TRUE(reported);
}

TEST(PipelineErrors, AlwaysPolicyPublishesErrorEventForTransientFailures) {
    test::EngineFixture fx;
    test::FakeOutput mq("Mq", true);
    test::FakeOutput webhook("Webhook", false);
    fx.registry.register_output(mq);
    fx.registry.register_output(webhook);
    mq.fail_ = true;

    auto ctx = fx.run(test::fltu_xml(), {.publish = true, .error_events = ErrorEventPolicy::Always});

    ASSERT_TRUE(ctx.error_event.has_value());
    EXPECT_TRUE(ctx.error_event_delivered);
    EXPECT_EQ(ctx.error_event->business_key, "I51569_2024-05-26");
    ASSERT_EQ(webhook.events().size(), 2u);
    EXPECT_EQ(webhook.events().back().event_type, "PIPELINE_ERROR");
}

TEST(PipelineErrors, DryRunPublishesNothing) {
    test::EngineFixture fx;
    test::FakeOutput output;
    fx.registry.register_output(output);

    auto ok = fx.run(test::fltu_xml(), {.publish = false, .error_events = ErrorEventPolicy::Never});
    auto bad = fx.run("<broken", {.publish = false, .error_events = ErrorEventPolicy::Always});

    EXPECT_FALSE(ok.failed());
    EXPECT_TRUE(bad.failed());
    EXPECT_TRUE(output.events().empty());
}

TEST(PipelineErrors, DisabledOutputsAreSkipped) {
    test::EngineFixture fx;
    test::FakeOutput output;
    output.enabled_ = false;
    fx.registry.register_output(output);

    auto ctx = fx.run(test::fltu_xml());
    EXPECT_FALSE(ctx.failed());
    EXPECT_TRUE(output.events().empty());
    EXPECT_TRUE(ctx.publish_reports.empty());
}
