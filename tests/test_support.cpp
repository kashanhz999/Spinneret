#include "test_support.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "spinneret/util/files.h"
#include "spinneret/util/logging.h"
#include "spinneret/util/random.h"

namespace spinneret::test {

namespace fs = std::filesystem;

namespace {

class QuietLogs : public ::testing::Environment {
public:
    void SetUp() override { util::Log::set_level(util::LogLevel::Error); }
};

[[maybe_unused]] const auto* kQuietLogs = ::testing::AddGlobalTestEnvironment(new QuietLogs);

}  // namespace

TempDir::TempDir() : path_(fs::temp_directory_path() / ("spinneret-test-" + util::random_uuid())) {
    fs::create_directories(path_);
}

TempDir::~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
}

pipeline::PipelineContext EngineFixture::run(const std::string& xml, const pipeline::IngestOptions& options) const {
    pipeline::PipelineContext context(xml, domain::CorrelationMetadata::create(std::nullopt, "TEST"));
    engine.ingest(context, options);
    return context;
}

std::string read_sample(const std::string& relative_path) {
    auto content = util::read_file(fs::path(SPINNERET_SAMPLES_DIR) / relative_path);
    if (!content) throw std::runtime_error("Missing sample " + relative_path);
    return *content;
}

std::vector<fs::path> all_samples() {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(SPINNERET_SAMPLES_DIR)) {
        if (entry.is_regular_file() && entry.path().extension() == ".xml") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string fltu_xml(const std::string& sub_type, const std::string& message_type) {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<yiapl:YIAPL_FlightLegNotifRQ xmlns:yiapl="yiapl.co.in/root/schema" xmlns:aidx="http://www.iata.org/IATA/2007/00">
  <yiapl:YIAPL_Header>
    <yiapl:SequenceNumber>42</yiapl:SequenceNumber>
    <yiapl:SendingSystem>AODB</yiapl:SendingSystem>
    <yiapl:MessageType>)" +
           message_type + R"(</yiapl:MessageType>
    <yiapl:MessageSubType>)" +
           sub_type + R"(</yiapl:MessageSubType>
    <yiapl:OriginatorTimeStamp>2024-05-27T06:47:16.533Z</yiapl:OriginatorTimeStamp>
  </yiapl:YIAPL_Header>
  <aidx:IATA_AIDX_FlightLegNotifRQ Version="21.3">
    <aidx:FlightLeg>
      <aidx:LegIdentifier>
        <aidx:Airline CodeContext="3">I5</aidx:Airline>
        <aidx:FlightNumber>1569</aidx:FlightNumber>
        <aidx:OriginDate>2024-05-26</aidx:OriginDate>
      </aidx:LegIdentifier>
    </aidx:FlightLeg>
  </aidx:IATA_AIDX_FlightLegNotifRQ>
</yiapl:YIAPL_FlightLegNotifRQ>)";
}

}  // namespace spinneret::test
