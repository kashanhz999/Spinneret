#include "spinneret/domain/message_types.h"

#include <array>

namespace spinneret::domain {

namespace {

constexpr std::array<IntentEntry, 9> kMasterData = {{
    {"ARPT", "MASTER_AIRPORT"},
    {"ARLN", "MASTER_AIRLINE"},
    {"ATYP", "MASTER_AIRCRAFT_TYPE"},
    {"AREG", "MASTER_AIRCRAFT_REG"},
    {"GATE", "MASTER_GATE"},
    {"STND", "MASTER_STAND"},
    {"CHCK", "MASTER_CHECKIN"},
    {"BELT", "MASTER_CAROUSEL"},
    {"MAKE", "MASTER_MAKEUP"},
}};

constexpr std::array<IntentEntry, 2> kFlightSchedule = {{
    {"DALY", "FLIGHT_SCHEDULE_DAILY"},
    {"ADHC", "FLIGHT_SCHEDULE_ADHOC"},
}};

constexpr std::array<IntentEntry, 38> kFlightUpdate = {{
    {"FETU", "FLIGHT_ESTIMATED_TIME_UPDATE"},
    {"FATU", "FLIGHT_ACTUAL_TIME_UPDATE"},
    {"FSTY", "FLIGHT_SERVICE_TYPE_UPDATE"},
    {"FSTN", "FLIGHT_STAND_BAY_UPDATE"},
    {"FGAT", "FLIGHT_GATE_UPDATE"},
    {"FRCR", "FLIGHT_RECLAIM_CAROUSEL_UPDATE"},
    {"FCKI", "FLIGHT_CHECKIN_COUNTER_UPDATE"},
    {"FACT", "FLIGHT_AIRCRAFT_TYPE_UPDATE"},
    {"FREG", "FLIGHT_REGISTRATION_UPDATE"},
    {"FTRM", "FLIGHT_TERMINAL_UPDATE"},
    {"FRUT", "FLIGHT_ROUTE_UPDATE"},
    {"FCOS", "FLIGHT_CODE_SHARE_UPDATE"},
    {"FSTA", "FLIGHT_STATUS_UPDATE"},
    {"FGHA", "FLIGHT_HANDLING_AGENT_UPDATE"},
    {"FRMK", "FLIGHT_REMARKS_UPDATE"},
    {"FDLY", "FLIGHT_DELAY_CODE_UPDATE"},
    {"FMCR", "FLIGHT_MAKEUP_CAROUSEL_UPDATE"},
    {"FDEL", "FLIGHT_DELETION"},
    {"MCRQ", "FLIGHT_MAKEUP_CAROUSEL_REQUEST"},
    {"FLAD", "FLIGHT_LOAD_UPDATE"},
    {"FLIU", "FLIGHT_INDICATOR_UPDATE"},
    {"FORR", "FLIGHT_ON_OFF_BLOCK_RESEND_REQUEST"},
    {"RNUP", "RUNWAY_UPDATE"},
    {"FPLA", "ATC_FLIGHT_PLAN_ACTIVATION"},
    {"OTOT", "TAKEOFF_OUTSTATION"},
    {"INRU", "LOCAL_RADAR_UPDATE"},
    {"TMOU", "FINAL_APPROACH"},
    {"ELDT", "ESTIMATED_LANDING_TIME_UPDATE"},
    {"ALDT", "ACTUAL_LANDING_TIME_UPDATE"},
    {"ACGT", "GROUND_HANDLING_START"},
    {"TSAT", "TSAT_ISSUE"},
    {"ASBT", "START_BOARDING"},
    {"ARDT", "AIRCRAFT_READY"},
    {"ASRT", "STARTUP_REQUEST"},
    {"ASAT", "STARTUP_APPROVED"},
    {"CTOT", "CALCULATED_TAKEOFF_TIME_UPDATE"},
    {"ATOT", "ACTUAL_TAKEOFF_TIME_UPDATE"},
    {"FSIO", "FLIGHT_STAND_IN_OUT_TIME_UPDATE"},
}};

constexpr std::array<IntentEntry, 2> kHealth = {{
    {"HLCK", "HEALTH_CHECK_REQUEST"},
    {"HLOK", "HEALTH_CHECK_OK"},
}};

}  // namespace

std::optional<MessageFamily> parse_family(std::string_view code) {
    if (code == "MSTR") return MessageFamily::MSTR;
    if (code == "FLSH") return MessageFamily::FLSH;
    if (code == "FLTU") return MessageFamily::FLTU;
    if (code == "HLTH") return MessageFamily::HLTH;
    return std::nullopt;
}

std::string_view to_string(MessageFamily family) {
    switch (family) {
        case MessageFamily::MSTR: return "MSTR";
        case MessageFamily::FLSH: return "FLSH";
        case MessageFamily::FLTU: return "FLTU";
        case MessageFamily::HLTH: return "HLTH";
    }
    return "UNKNOWN";
}

std::span<const IntentEntry> intents(MessageFamily family) {
    switch (family) {
        case MessageFamily::MSTR: return kMasterData;
        case MessageFamily::FLSH: return kFlightSchedule;
        case MessageFamily::FLTU: return kFlightUpdate;
        case MessageFamily::HLTH: return kHealth;
    }
    return {};
}

std::optional<std::string_view> resolve_intent(MessageFamily family, std::string_view sub_type) {
    for (const auto& [code, intent] : intents(family)) {
        if (code == sub_type) return intent;
    }
    return std::nullopt;
}

}  // namespace spinneret::domain
