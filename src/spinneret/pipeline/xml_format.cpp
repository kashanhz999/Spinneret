#include "spinneret/pipeline/xml_format.h"

#include <sstream>

#include "spinneret/pipeline/pipeline_error.h"
#include "spinneret/pipeline/stages.h"

namespace spinneret::pipeline {

std::string pretty_print_xml(std::string_view raw, const pugi::xml_document* document) {
    std::unique_ptr<pugi::xml_document> parsed;
    if (!document) {
        try {
            parsed = parse_xml(raw);
        } catch (const PipelineError&) {
            return std::string(raw);
        }
        document = parsed.get();
    }

    std::ostringstream out;
    document->save(out, "  ", pugi::format_indent, pugi::encoding_utf8);
    return out.str();
}

}  // namespace spinneret::pipeline
