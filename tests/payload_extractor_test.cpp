#include <gtest/gtest.h>

#include "spinneret/pipeline/stages.h"

using namespace spinneret;
using json = nlohmann::ordered_json;

namespace {

json convert(const std::string& xml) {
    auto document = pipeline::parse_xml(xml);
    return pipeline::PayloadExtractor::to_json(*document);
}

}  // namespace

TEST(PayloadExtractor, PreservesPrefixesAttributesAndText) {
    auto j = convert(R"(<a:Root xmlns:a="urn:a" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
                         xsi:schemaLocation="urn:a a.xsd" Version="21.3">
                          <a:Airline CodeContext="3">I5</a:Airline>
                        </a:Root>)");

    ASSERT_TRUE(j.contains("Root"));
    const auto& root = j["Root"];
    EXPECT_EQ(root["__prefix"], "a");
    EXPECT_EQ(root["_Version"], "21.3");
    EXPECT_EQ(root["_schemaLocation"], "urn:a a.xsd");
    EXPECT_FALSE(root.contains("_a")) << "xmlns declarations must be skipped";
    EXPECT_FALSE(root.contains("_xsi"));
    EXPECT_EQ(root["Airline"]["__prefix"], "a");
    EXPECT_EQ(root["Airline"]["_CodeContext"], "3");
    EXPECT_EQ(root["Airline"]["__text"], "I5");
}

TEST(PayloadExtractor, RepeatedElementsBecomeArraysAndOrderIsKept) {
    auto j = convert("<R><z>1</z><leg>A</leg><y/><leg>B</leg><leg>C</leg></R>");
    const auto& r = j["R"];

    ASSERT_TRUE(r["leg"].is_array());
    ASSERT_EQ(r["leg"].size(), 3u);
    EXPECT_EQ(r["leg"][1]["__text"], "B");
    EXPECT_TRUE(r["y"].is_object());
    EXPECT_TRUE(r["y"].empty());

    std::vector<std::string> keys;
    for (const auto& [key, value] : r.items()) keys.push_back(key);
    EXPECT_EQ(keys, (std::vector<std::string>{"z", "leg", "y"}));
}

TEST(PayloadExtractor, CdataEntitiesAndWhitespace) {
    auto j = convert("<R><a>  x &amp; y  </a><b><![CDATA[<raw>]]></b><c>\n   \n</c></R>");
    EXPECT_EQ(j["R"]["a"]["__text"], "x & y");
    EXPECT_EQ(j["R"]["b"]["__text"], "<raw>");
    EXPECT_FALSE(j["R"]["c"].contains("__text"));
}

TEST(PayloadExtractor, KeyOrderMatchesSxpm) {
    auto j = convert(R"(<p:E xmlns:p="urn:p" k="v">text<p:child/></p:E>)");
    std::vector<std::string> keys;
    for (const auto& [key, value] : j["E"].items()) keys.push_back(key);
    EXPECT_EQ(keys, (std::vector<std::string>{"__prefix", "_k", "__text", "child"}));
}

TEST(PayloadExtractor, RejectsMalformedXmlWithPosition) {
    try {
        pipeline::parse_xml("<root>\n  <child>\n</root>");
        FAIL() << "expected PipelineError";
    } catch (const pipeline::PipelineError& e) {
        EXPECT_EQ(e.kind(), pipeline::ErrorKind::InvalidXml);
        EXPECT_NE(std::string(e.what()).find("line 3"), std::string::npos) << e.what();
    }
}

TEST(PayloadExtractor, RejectsEmptyAndMultiRootDocuments) {
    EXPECT_THROW(pipeline::parse_xml("   "), pipeline::PipelineError);
    EXPECT_THROW(pipeline::parse_xml("<a/><b/>"), pipeline::PipelineError);
    EXPECT_THROW(pipeline::parse_xml("<a/>trailing"), pipeline::PipelineError);
}

TEST(PayloadExtractor, RejectsExcessiveNesting) {
    std::string xml;
    for (int i = 0; i < 300; ++i) xml += "<n>";
    for (int i = 0; i < 300; ++i) xml += "</n>";
    auto document = pipeline::parse_xml(xml);
    EXPECT_THROW(pipeline::PayloadExtractor::to_json(*document), pipeline::PipelineError);
}

TEST(PayloadExtractor, DoesNotResolveExternalEntities) {
    auto document = pipeline::parse_xml(
        R"(<?xml version="1.0"?><!DOCTYPE r [<!ENTITY x SYSTEM "file:///etc/passwd">]><r>&x;</r>)");
    auto j = pipeline::PayloadExtractor::to_json(*document);
    EXPECT_EQ(j["r"]["__text"], "&x;");
}
