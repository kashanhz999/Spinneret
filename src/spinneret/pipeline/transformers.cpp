#include "spinneret/pipeline/transformers.h"

#include <format>

#include "spinneret/util/time.h"

namespace spinneret::pipeline {

namespace {

using json = nlohmann::ordered_json;

const json* find_object_field(const json& node, std::string_view field) {
    if (!node.is_object()) return nullptr;
    if (auto it = node.find(field); it != node.end()) return &*it;
    for (const auto& [key, value] : node.items()) {
        if (value.is_object()) {
            if (const json* found = find_object_field(value, field)) return found;
        }
    }
    return nullptr;
}

std::string text_of(const json& parent, std::string_view field) {
    const json* child = find_object_field(parent, field);
    if (!child) return {};
    if (child->is_object()) {
        if (auto it = child->find("__text"); it != child->end() && it->is_string()) {
            return it->get<std::string>();
        }
        return {};
    }
    if (child->is_string()) return child->get<std::string>();
    if (child->is_primitive() && !child->is_null()) return child->dump();
    return {};
}

}  // namespace

std::string flight_leg_key(const json& tree, std::string_view family_prefix) {
    const json* leg = find_object_field(tree, "LegIdentifier");
    if (!leg) return std::format("{}_{}", family_prefix, util::epoch_millis(util::Clock::now()));

    std::string airline = text_of(*leg, "Airline");
    std::string flight_number = text_of(*leg, "FlightNumber");
    std::string origin_date = text_of(*leg, "OriginDate");

    std::string key = (airline.empty() ? "XX" : airline) + (flight_number.empty() ? "0000" : flight_number);
    if (!origin_date.empty()) key += "_" + origin_date;
    return key;
}

PassthroughTransformer::PassthroughTransformer(std::string name, domain::MessageFamily family,
                                               KeyStrategy strategy)
    : name_(std::move(name)), family_(family), strategy_(strategy) {}

std::string PassthroughTransformer::business_key(const json& tree, const PipelineContext& context) const {
    std::string_view prefix = domain::to_string(family_);
    if (strategy_ == KeyStrategy::FlightLeg) return flight_leg_key(tree, prefix);

    std::string sub_type = context.header ? context.header->message_sub_type : std::string{};
    return std::format("{}_{}_{}", prefix, sub_type.empty() ? "UNKNOWN" : sub_type,
                       util::epoch_millis(util::Clock::now()));
}

std::vector<domain::FlightEvent> PassthroughTransformer::transform(const json& tree,
                                                                   const PipelineContext& context) const {
    domain::FlightEvent event{
        .event_id = context.correlation().span_id,
        .trace_id = context.correlation().trace_id,
        .event_type = context.intent,
        .business_key = business_key(tree, context),
        .event_time = util::Clock::now(),
        .payload = tree,
    };
    std::vector<domain::FlightEvent> events;
    events.push_back(std::move(event));
    return events;
}

std::vector<std::unique_ptr<EventTransformer>> default_transformers() {
    using domain::MessageFamily;
    using Strategy = PassthroughTransformer::KeyStrategy;

    std::vector<std::unique_ptr<EventTransformer>> list;
    list.push_back(std::make_unique<PassthroughTransformer>("FlightScheduleTransformer",
                                                            MessageFamily::FLSH, Strategy::FlightLeg));
    list.push_back(std::make_unique<PassthroughTransformer>("FlightUpdateTransformer",
                                                            MessageFamily::FLTU, Strategy::FlightLeg));
    list.push_back(std::make_unique<PassthroughTransformer>("MasterDataTransformer", MessageFamily::MSTR,
                                                            Strategy::SubTypeTimestamp));
    list.push_back(std::make_unique<PassthroughTransformer>("HealthTransformer", MessageFamily::HLTH,
                                                            Strategy::SubTypeTimestamp));
    return list;
}

TransformerRegistry::TransformerRegistry(std::vector<std::unique_ptr<EventTransformer>> transformers)
    : transformers_(std::move(transformers)) {}

void TransformerRegistry::execute(PipelineContext& context) const {
    if (!context.family || !context.parsed) {
        throw PipelineError(ErrorKind::Internal, "Transformation requires a resolved intent and parsed payload");
    }

    const EventTransformer* transformer = nullptr;
    for (const auto& candidate : transformers_) {
        if (candidate->supports(*context.family)) {
            transformer = candidate.get();
            break;
        }
    }
    if (!transformer) {
        throw PipelineError(ErrorKind::TransformFailed,
                            std::format("No transformer registered for message family {}",
                                        domain::to_string(*context.family)));
    }

    std::vector<domain::FlightEvent> events;
    try {
        events = transformer->transform(*context.parsed, context);
    } catch (const PipelineError&) {
        throw;
    } catch (const std::exception& e) {
        throw PipelineError(ErrorKind::TransformFailed,
                            std::format("{} failed: {}", transformer->name(), e.what()));
    }

    for (auto& event : events) context.events.push_back(std::move(event));
    context.add_audit(std::format("Stage 4: Transformed {} events via {}", events.size(), transformer->name()));
}

}  // namespace spinneret::pipeline
