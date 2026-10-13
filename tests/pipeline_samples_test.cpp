// Feeds every sample XML from SXPM's test corpus through the full pipeline
// (the C++ counterpart of SXPM's PipelineIntegrationTest).

#include <cctype>

#include <gtest/gtest.h>

#include "spinneret/util/files.h"
#include "test_support.h"

using namespace spinneret;

class SampleFiles : public ::testing::TestWithParam<std::filesystem::path> {};

TEST_P(SampleFiles, ProcessesAndPublishesOneEvent) {
    test::EngineFixture fx;
    test::FakeOutput output;
    fx.registry.register_output(output);

    auto xml = util::read_file(GetParam());
    ASSERT_TRUE(xml.has_value());
    auto ctx = fx.run(*xml);

    ASSERT_FALSE(ctx.failed()) << ctx.error->what();
    ASSERT_EQ(ctx.events.size(), 1u);
    const auto& event = ctx.events.front();
    EXPECT_EQ(event.event_type, ctx.intent);
    EXPECT_EQ(event.trace_id, ctx.correlation().trace_id);
    EXPECT_FALSE(event.business_key.empty());
    EXPECT_TRUE(event.payload.is_object());
    EXPECT_EQ(event.payload.size(), 1u) << "payload is wrapped in the root element's local name";
    ASSERT_EQ(output.events().size(), 1u);
}

INSTANTIATE_TEST_SUITE_P(Corpus, SampleFiles, ::testing::ValuesIn(test::all_samples()),
                         [](const ::testing::TestParamInfo<std::filesystem::path>& info) {
                             std::string name = std::to_string(info.index) + "_" + info.param.stem().string();
                             for (char& c : name) {
                                 if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                             }
                             return name;
                         });

TEST(SampleCorpus, CoversAllFamilies) {
    EXPECT_GE(test::all_samples().size(), 120u);
}

TEST(BusinessKey, FlightUpdateUsesLegIdentifier) {
    test::EngineFixture fx;
    auto ctx = fx.run(test::read_sample("FLTU/FGAT/Flight Gate Update FGAT.xml"), {.publish = false});
    ASSERT_FALSE(ctx.failed());
    EXPECT_EQ(ctx.intent, "FLIGHT_GATE_UPDATE");
    EXPECT_EQ(ctx.events.front().business_key, "I51569_2024-05-26");
}

TEST(BusinessKey, MultiLegScheduleFallsBackToTimestamp) {
    test::EngineFixture fx;
    auto ctx = fx.run(test::read_sample("FLSH/DALY/DALY.xml"), {.publish = false});
    ASSERT_FALSE(ctx.failed());
    EXPECT_TRUE(ctx.events.front().business_key.starts_with("FLSH_")) << ctx.events.front().business_key;
}

TEST(BusinessKey, MasterDataUsesSubtype) {
    test::EngineFixture fx;
    auto ctx = fx.run(test::read_sample("MasterDataNotifRQ/GATE/GATE(Create).xml"), {.publish = false});
    ASSERT_FALSE(ctx.failed());
    EXPECT_EQ(ctx.intent, "MASTER_GATE");
    EXPECT_TRUE(ctx.events.front().business_key.starts_with("MSTR_GATE_"));
}

TEST(EventJson, MatchesTheSxpmWireContract) {
    test::EngineFixture fx;
    auto ctx = fx.run(test::read_sample("FLTU/FGAT/Flight Gate Update FGAT.xml"), {.publish = false});
    auto j = domain::to_json(ctx.events.front());

    std::vector<std::string> keys;
    for (const auto& [key, value] : j.items()) keys.push_back(key);
    EXPECT_EQ(keys, (std::vector<std::string>{"eventId", "traceId", "eventType", "businessKey", "eventTime",
                                              "payloadJson"}));
    EXPECT_TRUE(j["payloadJson"].is_object()) << "payloadJson is an embedded object, not a string";
    EXPECT_EQ(j["payloadJson"]["YIAPL_FlightLegNotifRQ"]["YIAPL_Header"]["MessageType"]["__text"], "FLTU");
}
