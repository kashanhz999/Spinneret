#include <gtest/gtest.h>

#include "spinneret/domain/message_types.h"
#include "spinneret/pipeline/stages.h"
#include "test_support.h"

using namespace spinneret;

namespace {

pipeline::PipelineContext context_for(const std::string& xml) {
    return pipeline::PipelineContext(xml, domain::CorrelationMetadata::create(std::nullopt, "TEST"));
}

pipeline::ErrorKind failure_kind(const std::string& xml) {
    auto ctx = context_for(xml);
    try {
        pipeline::HeaderExtractor{}.execute(ctx);
        pipeline::IntentResolver{}.execute(ctx);
    } catch (const pipeline::PipelineError& e) {
        return e.kind();
    }
    ADD_FAILURE() << "expected a PipelineError";
    return pipeline::ErrorKind::Internal;
}

}  // namespace

TEST(HeaderExtractor, ReadsHeaderFields) {
    auto ctx = context_for(test::fltu_xml("FGAT"));
    pipeline::HeaderExtractor{}.execute(ctx);

    ASSERT_TRUE(ctx.header.has_value());
    EXPECT_EQ(ctx.header->sequence_number, "42");
    EXPECT_EQ(ctx.header->message_type, "FLTU");
    EXPECT_EQ(ctx.header->message_sub_type, "FGAT");
    EXPECT_EQ(ctx.header->sending_system, "AODB");
    EXPECT_TRUE(ctx.header->originator_time.has_value());
    EXPECT_FALSE(ctx.header->message_type_deduced);
    EXPECT_NE(ctx.document, nullptr);
}

TEST(HeaderExtractor, PrefersFieldsInsideYiaplHeader) {
    // A Terminal-style collision: MessageType appears in the body before the header.
    auto ctx = context_for(R"(<Root><Body><MessageType>WRONG</MessageType></Body>
        <YIAPL_Header><MessageType>FLTU</MessageType><MessageSubType>FGAT</MessageSubType></YIAPL_Header></Root>)");
    pipeline::HeaderExtractor{}.execute(ctx);
    EXPECT_EQ(ctx.header->message_type, "FLTU");
}

TEST(HeaderExtractor, DeducesTypeFromRootElement) {
    auto ctx = context_for(R"(<y:YIAPL_MasterDataNotifRQ xmlns:y="urn:y"><y:YIAPL_Header>
        <y:MessageSubType>GATE</y:MessageSubType></y:YIAPL_Header></y:YIAPL_MasterDataNotifRQ>)");
    pipeline::HeaderExtractor{}.execute(ctx);
    EXPECT_EQ(ctx.header->message_type, "MSTR");
    EXPECT_TRUE(ctx.header->message_type_deduced);
}

TEST(IntentResolver, ResolvesEveryDefinedSubtype) {
    for (auto family : {domain::MessageFamily::MSTR, domain::MessageFamily::FLSH, domain::MessageFamily::FLTU,
                        domain::MessageFamily::HLTH}) {
        for (const auto& [sub_type, intent] : domain::intents(family)) {
            auto ctx = context_for(test::fltu_xml(std::string(sub_type), std::string(domain::to_string(family))));
            pipeline::HeaderExtractor{}.execute(ctx);
            pipeline::IntentResolver{}.execute(ctx);
            EXPECT_EQ(ctx.intent, intent);
            EXPECT_EQ(ctx.family, family);
        }
    }
}

TEST(IntentResolver, SubtypeCountsMatchSxpm) {
    EXPECT_EQ(domain::intents(domain::MessageFamily::MSTR).size(), 9u);
    EXPECT_EQ(domain::intents(domain::MessageFamily::FLSH).size(), 2u);
    EXPECT_EQ(domain::intents(domain::MessageFamily::FLTU).size(), 38u);
    EXPECT_EQ(domain::intents(domain::MessageFamily::HLTH).size(), 2u);
}

TEST(IntentResolver, NormalisesCaseAndWhitespace) {
    auto ctx = context_for(test::fltu_xml("  fgat ", " fltu"));
    pipeline::HeaderExtractor{}.execute(ctx);
    pipeline::IntentResolver{}.execute(ctx);
    EXPECT_EQ(ctx.intent, "FLIGHT_GATE_UPDATE");
}

TEST(IntentResolver, RejectsUnknownTypesAsPermanentFailures) {
    EXPECT_EQ(failure_kind(test::fltu_xml("FGAT", "XXXX")), pipeline::ErrorKind::UnknownMessageType);
    EXPECT_EQ(failure_kind(test::fltu_xml("ZZZZ", "FLTU")), pipeline::ErrorKind::UnknownMessageType);
    EXPECT_EQ(failure_kind(test::fltu_xml("", "FLTU")), pipeline::ErrorKind::UnknownMessageType);
    EXPECT_EQ(failure_kind("<Unrelated><x/></Unrelated>"), pipeline::ErrorKind::UnknownMessageType);
    EXPECT_TRUE(pipeline::is_permanent(pipeline::ErrorKind::UnknownMessageType));
}
