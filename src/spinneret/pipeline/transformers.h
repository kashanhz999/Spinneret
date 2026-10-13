#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "spinneret/domain/flight_event.h"
#include "spinneret/domain/message_types.h"
#include "spinneret/pipeline/pipeline_context.h"

namespace spinneret::pipeline {

/// Turns a parsed message into canonical events. Extension point: add a new
/// implementation to change what a family emits.
class EventTransformer {
public:
    virtual ~EventTransformer() = default;
    virtual std::string_view name() const = 0;
    virtual bool supports(domain::MessageFamily family) const = 0;
    virtual std::vector<domain::FlightEvent> transform(const nlohmann::ordered_json& tree,
                                                       const PipelineContext& context) const = 0;
};

/// Emits one event carrying the complete, unaltered JSON tree. Only the
/// business key differs between families.
class PassthroughTransformer final : public EventTransformer {
public:
    enum class KeyStrategy {
        FlightLeg,         ///< `<Airline><FlightNumber>_<OriginDate>` from the first LegIdentifier.
        SubTypeTimestamp,  ///< `<FAMILY>_<SubType>_<epochMillis>`.
    };

    PassthroughTransformer(std::string name, domain::MessageFamily family, KeyStrategy strategy);

    std::string_view name() const override { return name_; }
    bool supports(domain::MessageFamily family) const override { return family == family_; }
    std::vector<domain::FlightEvent> transform(const nlohmann::ordered_json& tree,
                                               const PipelineContext& context) const override;

private:
    std::string business_key(const nlohmann::ordered_json& tree, const PipelineContext& context) const;

    std::string name_;
    domain::MessageFamily family_;
    KeyStrategy strategy_;
};

/// Business key from the first `LegIdentifier` object found by depth-first
/// search (arrays are not searched, so multi-leg schedules fall back to
/// `<FAMILY>_<epochMillis>`).
std::string flight_leg_key(const nlohmann::ordered_json& tree, std::string_view family_prefix);

/// The standard transformers for MSTR, FLSH, FLTU and HLTH.
std::vector<std::unique_ptr<EventTransformer>> default_transformers();

/// **Stage 4 — Transformation.** Dispatches to the first transformer that
/// supports the message family.
class TransformerRegistry {
public:
    explicit TransformerRegistry(std::vector<std::unique_ptr<EventTransformer>> transformers);

    void execute(PipelineContext& context) const;

private:
    std::vector<std::unique_ptr<EventTransformer>> transformers_;
};

}  // namespace spinneret::pipeline
