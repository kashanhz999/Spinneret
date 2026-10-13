#include <algorithm>
#include <format>

#include "spinneret/pipeline/stages.h"
#include "spinneret/util/logging.h"
#include "spinneret/util/strings.h"

namespace spinneret::pipeline {

namespace {

const util::Logger kLog{"pipeline.header"};

std::pair<std::size_t, std::size_t> line_and_column(std::string_view text, std::ptrdiff_t offset) {
    std::size_t end = std::min<std::size_t>(offset < 0 ? 0 : static_cast<std::size_t>(offset),
                                            text.size());
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < end; ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

pugi::xml_node find_descendant(pugi::xml_node scope, std::string_view local) {
    return scope.find_node([local](pugi::xml_node n) {
        return n.type() == pugi::node_element && util::local_name(n.name()) == local;
    });
}

void append_text(pugi::xml_node node, std::string& out) {
    for (pugi::xml_node child : node.children()) {
        switch (child.type()) {
            case pugi::node_pcdata:
            case pugi::node_cdata: out += child.value(); break;
            case pugi::node_element: append_text(child, out); break;
            default: break;
        }
    }
}

std::string field(pugi::xml_node scope, std::string_view local) {
    pugi::xml_node node = find_descendant(scope, local);
    if (!node) return {};
    std::string text;
    append_text(node, text);
    return std::string(util::trim(text));
}

std::string deduce_type_from_root(std::string_view root_name) {
    if (root_name.find("FlightLegNotif") != std::string_view::npos) return "FLSH";
    if (root_name.find("FlightLegRS") != std::string_view::npos) return "HLTH";
    if (root_name.find("connect") != std::string_view::npos ||
        root_name.find("MasterData") != std::string_view::npos) {
        return "MSTR";
    }
    return {};
}

}  // namespace

std::unique_ptr<pugi::xml_document> parse_xml(std::string_view xml) {
    if (util::trim(xml).empty()) {
        throw PipelineError(ErrorKind::InvalidXml, "Empty XML input");
    }

    auto document = std::make_unique<pugi::xml_document>();
    // Fragment mode keeps stray top-level text as nodes so the checks below can reject it.
    pugi::xml_parse_result result = document->load_buffer(
        xml.data(), xml.size(), pugi::parse_default | pugi::parse_fragment, pugi::encoding_auto);
    if (!result) {
        auto [line, column] = line_and_column(xml, result.offset);
        throw PipelineError(ErrorKind::InvalidXml,
                            std::format("Invalid XML at line {}, column {}: {}", line, column,
                                        result.description()));
    }

    int roots = 0;
    for (pugi::xml_node child : document->children()) {
        if (child.type() == pugi::node_element) {
            ++roots;
        } else if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            throw PipelineError(ErrorKind::InvalidXml, "Invalid XML: text outside the root element");
        }
    }
    if (roots == 0) throw PipelineError(ErrorKind::InvalidXml, "Invalid XML: no root element");
    if (roots > 1) throw PipelineError(ErrorKind::InvalidXml, "Invalid XML: multiple root elements");

    return document;
}

void HeaderExtractor::execute(PipelineContext& context) const {
    kLog.debug("Extracting YIAPL header ({} chars)", context.raw_input().size());

    context.document = parse_xml(context.raw_input());
    pugi::xml_node root = context.document->document_element();

    pugi::xml_node scope = find_descendant(root, "YIAPL_Header");
    if (!scope) scope = root;

    domain::YiaplHeader header;
    header.sequence_number = field(scope, "SequenceNumber");
    header.message_type = field(scope, "MessageType");
    header.message_sub_type = field(scope, "MessageSubType");
    header.sending_system = field(scope, "SendingSystem");
    header.originator_timestamp = field(scope, "OriginatorTimeStamp");

    if (!header.originator_timestamp.empty()) {
        header.originator_time = util::parse_iso8601(header.originator_timestamp);
        if (!header.originator_time) {
            kLog.warn("Could not parse OriginatorTimeStamp '{}'", header.originator_timestamp);
        }
    }

    if (header.message_type.empty()) {
        std::string_view root_name = util::local_name(root.name());
        header.message_type = deduce_type_from_root(root_name);
        if (header.message_type.empty()) {
            throw PipelineError(
                ErrorKind::UnknownMessageType,
                std::format("MessageType is missing and cannot be deduced from root element '{}'",
                            root_name));
        }
        header.message_type_deduced = true;
        kLog.warn("MessageType missing; deduced '{}' from root element '{}'", header.message_type,
                  root_name);
    }

    context.add_audit(std::format("Stage 1: Header Extracted ({})", header.message_type));
    context.header = std::move(header);
}

}  // namespace spinneret::pipeline
