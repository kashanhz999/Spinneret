#include "spinneret/config/config_service.h"

#include <format>
#include <mutex>

#include "spinneret/util/files.h"
#include "spinneret/util/logging.h"
#include "spinneret/util/strings.h"

namespace spinneret::config {

namespace fs = std::filesystem;

namespace {

const util::Logger kLog{"config"};

std::string type_name(const nlohmann::json& value) {
    if (value.is_boolean()) return "boolean";
    if (value.is_number_integer()) return "integer";
    if (value.is_string()) return "string";
    return value.type_name();
}

nlohmann::json parse_object(const std::string& text, const fs::path& source) {
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw ConfigError(ConfigError::Kind::Invalid, std::format("{} is not a JSON object", source.string()));
    }
    return parsed;
}

}  // namespace

const nlohmann::json& ConfigService::defaults() {
    static const nlohmann::json kDefaults = {
        // REST input
        {"adapters.input.rest.enabled", true},
        // IBM MQ input
        {"adapters.input.ibmmq.enabled", false},
        {"adapters.input.ibmmq.queue", "AODB.FLIGHT.UPDATE"},
        {"adapters.input.ibmmq.backoutThreshold", 0},  // 0 = use the queue's BOTHRESH, else 3
        {"adapters.input.ibmmq.backoutQueue", ""},     // empty = use the queue's BOQNAME
        // REST (webhook) output
        {"adapters.output.rest.enabled", false},
        {"adapters.output.rest.url", "http://localhost:3000/api/v1/webhook"},
        {"adapters.output.rest.required", false},
        {"adapters.output.rest.timeoutMs", 5000},
        // IBM MQ output
        {"adapters.output.ibmmq.enabled", false},
        {"adapters.output.ibmmq.queue", "AIRPORT.EVENTS.CANONICAL"},
        {"adapters.output.ibmmq.required", true},
        // IBM MQ connection (shared by input and output)
        {"ibm.mq.queueManager", "QM1"},
        {"ibm.mq.channel", "DEV.ADMIN.SVRCONN"},
        {"ibm.mq.connName", "localhost(1414)"},
        {"ibm.mq.user", ""},
        {"ibm.mq.password", ""},  // prefer the SPINNERET_MQ_PASSWORD environment variable
        {"ibm.mq.sslCipherSpec", ""},
        {"ibm.mq.sslKeyRepository", ""},
    };
    return kDefaults;
}

bool ConfigService::is_secret(std::string_view key) { return util::contains_ci(key, "password"); }

ConfigService::ConfigService(fs::path config_dir) : dir_(std::move(config_dir)) {}

void ConfigService::load() {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) throw ConfigError(ConfigError::Kind::Io, "Cannot create " + dir_.string() + ": " + ec.message());

    auto text = util::read_file(file());
    std::unique_lock lock(mutex_);
    if (!text) {
        kLog.info("No {} found; using defaults", file().string());
        overrides_ = json::object();
        return;
    }

    json loaded = parse_object(*text, file());
    json accepted = json::object();
    for (const auto& [key, value] : loaded.items()) {
        if (!defaults().contains(key)) {
            kLog.warn("Ignoring unknown configuration key '{}' in {}", key, file().string());
            continue;
        }
        validate(json{{key, value}});
        accepted[key] = value;
    }
    overrides_ = std::move(accepted);
    kLog.info("Loaded {} configuration override(s) from {}", overrides_.size(), file().string());
}

void ConfigService::validate(const json& properties) const {
    if (!properties.is_object()) {
        throw ConfigError(ConfigError::Kind::Invalid, "Configuration must be a JSON object");
    }
    for (const auto& [key, value] : properties.items()) {
        auto it = defaults().find(key);
        if (it == defaults().end()) {
            throw ConfigError(ConfigError::Kind::Invalid, std::format("Unknown configuration key '{}'", key));
        }
        if (value.is_null()) continue;  // reset to default

        const json& expected = *it;
        bool ok = (expected.is_boolean() && value.is_boolean()) ||
                  (expected.is_number_integer() && value.is_number_integer() && value.get<std::int64_t>() >= 0) ||
                  (expected.is_string() && value.is_string());
        if (!ok) {
            throw ConfigError(ConfigError::Kind::Invalid,
                              std::format("'{}' must be a {}{}, got {}", key, type_name(expected),
                                          expected.is_number_integer() ? " >= 0" : "", type_name(value)));
        }
    }
}

nlohmann::json ConfigService::get(std::string_view key) const {
    std::shared_lock lock(mutex_);
    std::string k(key);
    if (auto it = overrides_.find(k); it != overrides_.end()) return *it;
    if (auto it = defaults().find(k); it != defaults().end()) return *it;
    return nullptr;
}

bool ConfigService::get_bool(std::string_view key) const {
    json value = get(key);
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_string()) return util::to_lower(value.get<std::string>()) == "true";
    return false;
}

std::string ConfigService::get_string(std::string_view key) const {
    json value = get(key);
    if (value.is_string()) return value.get<std::string>();
    if (value.is_null()) return {};
    return value.dump();
}

std::int64_t ConfigService::get_int(std::string_view key) const {
    json value = get(key);
    if (value.is_number_integer()) return value.get<std::int64_t>();
    return 0;
}

nlohmann::json ConfigService::effective_locked() const {
    json result = defaults();
    for (const auto& [key, value] : overrides_.items()) result[key] = value;
    return result;
}

nlohmann::json ConfigService::effective() const {
    std::shared_lock lock(mutex_);
    return effective_locked();
}

nlohmann::json ConfigService::overrides() const {
    std::shared_lock lock(mutex_);
    return overrides_;
}

void ConfigService::save_locked() const {
    try {
        util::atomic_write(file(), overrides_.dump(2) + "\n", /*owner_only=*/true);
    } catch (const std::exception& e) {
        throw ConfigError(ConfigError::Kind::Io, e.what());
    }
}

namespace {

std::set<std::string> changed_keys(const nlohmann::json& before, const nlohmann::json& after) {
    std::set<std::string> changed;
    for (const auto& [key, value] : after.items()) {
        if (!before.contains(key) || before[key] != value) changed.insert(key);
    }
    return changed;
}

}  // namespace

std::set<std::string> ConfigService::update(const json& properties) {
    validate(properties);

    std::unique_lock lock(mutex_);
    const json before = effective_locked();
    const json previous_overrides = overrides_;

    for (const auto& [key, value] : properties.items()) {
        if (value.is_null()) {
            overrides_.erase(key);
        } else {
            overrides_[key] = value;
        }
    }

    try {
        if (auto current = util::read_file(file())) util::atomic_write(backup_file(), *current, true);
        save_locked();
    } catch (const std::exception& e) {
        overrides_ = previous_overrides;
        throw ConfigError(ConfigError::Kind::Io, std::format("Failed to persist configuration: {}", e.what()));
    }

    auto changed = changed_keys(before, effective_locked());
    kLog.info("Configuration updated; {} effective value(s) changed", changed.size());
    return changed;
}

std::set<std::string> ConfigService::restore_backup() {
    std::unique_lock lock(mutex_);
    auto backup_text = util::read_file(backup_file());
    if (!backup_text) throw ConfigError(ConfigError::Kind::NotFound, "No configuration backup exists");

    json restored = parse_object(*backup_text, backup_file());
    for (auto it = restored.begin(); it != restored.end();) {
        if (!defaults().contains(it.key())) {
            kLog.warn("Dropping unknown key '{}' from backup", it.key());
            it = restored.erase(it);
        } else {
            ++it;
        }
    }
    validate(restored);

    const json before = effective_locked();
    const json previous_overrides = overrides_;
    overrides_ = std::move(restored);
    try {
        util::atomic_write(backup_file(), previous_overrides.dump(2) + "\n", true);
        save_locked();
    } catch (const std::exception& e) {
        overrides_ = previous_overrides;
        throw ConfigError(ConfigError::Kind::Io, std::format("Failed to restore configuration: {}", e.what()));
    }

    auto changed = changed_keys(before, effective_locked());
    kLog.info("Configuration restored from backup; {} effective value(s) changed", changed.size());
    return changed;
}

}  // namespace spinneret::config
