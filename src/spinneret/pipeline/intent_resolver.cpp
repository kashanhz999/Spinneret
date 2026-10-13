#include <format>

#include "spinneret/pipeline/stages.h"
#include "spinneret/util/strings.h"

namespace spinneret::pipeline {

void IntentResolver::execute(PipelineContext& context) const {
    if (!context.header) {
        throw PipelineError(ErrorKind::Internal, "Header missing, cannot resolve intent");
    }

    const std::string type = util::to_upper(util::trim(context.header->message_type));
    const std::string sub_type = util::to_upper(util::trim(context.header->message_sub_type));

    if (type.empty()) {
        throw PipelineError(ErrorKind::UnknownMessageType, "MessageType is missing");
    }

    auto family = domain::parse_family(type);
    if (!family) {
        throw PipelineError(ErrorKind::UnknownMessageType, std::format("Unknown MessageType: {}", type));
    }

    auto intent = domain::resolve_intent(*family, sub_type);
    if (!intent) {
        throw PipelineError(
            ErrorKind::UnknownMessageType,
            sub_type.empty()
                ? std::format("MessageSubType is missing for MessageType '{}'", type)
                : std::format("Unknown MessageSubType '{}' for MessageType '{}'", sub_type, type));
    }

    context.family = family;
    context.intent = std::string(*intent);
    context.add_audit(std::format("Stage 2: Intent Resolved ({})", context.intent));
}

}  // namespace spinneret::pipeline
