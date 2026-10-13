#pragma once

#include <memory>
#include <string_view>

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include "spinneret/pipeline/pipeline_context.h"

namespace spinneret::pipeline {

/// Parses raw XML into a document, rejecting anything that is not a single
/// well-formed root element. Throws PipelineError(InvalidXml) with the line
/// and column of the problem.
std::unique_ptr<pugi::xml_document> parse_xml(std::string_view xml);

/// **Stage 1 — Header extraction.** Parses the XML (once, for all later
/// stages) and reads the `YIAPL_Header` fields. Searches inside the
/// `YIAPL_Header` element when present, otherwise anywhere in the document.
/// When `MessageType` is absent it is deduced from the root element name.
class HeaderExtractor {
public:
    void execute(PipelineContext& context) const;
};

/// **Stage 2 — Intent resolution.** Maps (MessageType, MessageSubType) to a
/// business intent such as FLIGHT_GATE_UPDATE. Unknown combinations throw
/// PipelineError(UnknownMessageType).
class IntentResolver {
public:
    void execute(PipelineContext& context) const;
};

/// **Stage 3 — Payload extraction.** Converts the whole XML tree to JSON
/// without dropping anything:
///
///  - namespace prefix  → `"__prefix"`
///  - attributes        → `"_<localName>"` (xmlns declarations are skipped)
///  - text and CDATA    → `"__text"` (trimmed, concatenated)
///  - child elements    → keyed by local name; repeated names become arrays
///
/// The result is wrapped as `{ "<rootLocalName>": { … } }` and keeps document
/// order.
class PayloadExtractor {
public:
    static constexpr int kMaxDepth = 256;

    void execute(PipelineContext& context) const;

    /// Conversion only, for callers that already hold a document.
    static nlohmann::ordered_json to_json(const pugi::xml_document& document);
};

}  // namespace spinneret::pipeline
