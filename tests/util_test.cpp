#include <regex>

#include <gtest/gtest.h>

#include "spinneret/domain/correlation_metadata.h"
#include "spinneret/util/random.h"
#include "spinneret/util/sha256.h"
#include "spinneret/util/strings.h"
#include "spinneret/util/time.h"

using namespace spinneret;

TEST(Sha256, MatchesKnownVectors) {
    EXPECT_EQ(util::sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(util::sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(util::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(util::sha256_hex(std::string(1000, 'a')),
              "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(Time, FormatsAndParsesIso8601) {
    auto tp = util::parse_iso8601("2024-05-27T06:47:16.533Z");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(util::iso8601_utc(*tp), "2024-05-27T06:47:16.533Z");

    EXPECT_EQ(util::iso8601_utc(*util::parse_iso8601("2024-12-04T00:00:00")), "2024-12-04T00:00:00.000Z");
    EXPECT_EQ(util::iso8601_utc(*util::parse_iso8601("2024-12-04T00:00:00.5Z")), "2024-12-04T00:00:00.500Z");
    EXPECT_FALSE(util::parse_iso8601("2024-13-01T00:00:00Z").has_value());
    EXPECT_FALSE(util::parse_iso8601("not a date").has_value());
    EXPECT_FALSE(util::parse_iso8601("2024-05-27T06:47:16.533+02:00").has_value());
}

TEST(Random, UuidsAreVersion4) {
    std::regex pattern("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");
    for (int i = 0; i < 100; ++i) EXPECT_TRUE(std::regex_match(util::random_uuid(), pattern));
    EXPECT_NE(util::random_uuid(), util::random_uuid());
    EXPECT_EQ(util::random_hex(32).size(), 64u);
}

TEST(Strings, QualifiedNames) {
    EXPECT_EQ(util::local_name("aidx:Airline"), "Airline");
    EXPECT_EQ(util::local_name("Airline"), "Airline");
    EXPECT_EQ(util::name_prefix("aidx:Airline"), "aidx");
    EXPECT_EQ(util::name_prefix("Airline"), "");
    EXPECT_EQ(util::trim("  x y \n"), "x y");
}

TEST(Correlation, AdoptsOnlySafeUpstreamTraceIds) {
    EXPECT_EQ(domain::CorrelationMetadata::create("abc-123", "T").trace_id, "abc-123");
    EXPECT_NE(domain::CorrelationMetadata::create("bad\nid", "T").trace_id, "bad\nid");
    EXPECT_NE(domain::CorrelationMetadata::create(std::string(200, 'a'), "T").trace_id, std::string(200, 'a'));
    auto meta = domain::CorrelationMetadata::create(std::nullopt, "T");
    EXPECT_FALSE(meta.trace_id.empty());
    EXPECT_NE(meta.trace_id, meta.span_id);
}
