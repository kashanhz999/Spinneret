#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace spinneret::config {

class ConfigError : public std::runtime_error {
public:
    enum class Kind { Invalid, NotFound, Io };

    ConfigError(Kind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    Kind kind() const noexcept { return kind_; }

private:
    Kind kind_;
};

/// Runtime configuration stored in `<configDir>/runtime-config.json`.
///
/// Every supported key has a default (see defaults()); the file holds only
/// overrides. Adapters read values on each use, so changes apply without a
/// restart. Updates are validated (unknown keys and wrong types are rejected),
/// the previous file is kept as `config-backup.json`, and writes are atomic.
class ConfigService {
public:
    using json = nlohmann::json;

    explicit ConfigService(std::filesystem::path config_dir);

    /// Creates the directory if needed and loads overrides. Throws ConfigError
    /// if the file exists but is corrupt.
    void load();

    const std::filesystem::path& dir() const { return dir_; }

    /// Effective value (override, else default); null for unknown keys.
    json get(std::string_view key) const;
    bool get_bool(std::string_view key) const;
    std::string get_string(std::string_view key) const;
    std::int64_t get_int(std::string_view key) const;

    /// All keys with their effective values.
    json effective() const;
    json overrides() const;

    /// Applies `properties` (a JSON object; null removes an override).
    /// Returns the keys whose effective value changed.
    std::set<std::string> update(const json& properties);

    /// Swaps the current file with the backup (so a restore can itself be
    /// undone). Returns the keys whose effective value changed.
    std::set<std::string> restore_backup();

    static const json& defaults();
    static bool is_secret(std::string_view key);

private:
    std::filesystem::path file() const { return dir_ / "runtime-config.json"; }
    std::filesystem::path backup_file() const { return dir_ / "config-backup.json"; }

    void validate(const json& properties) const;
    json effective_locked() const;
    void save_locked() const;

    std::filesystem::path dir_;
    mutable std::shared_mutex mutex_;
    json overrides_ = json::object();
};

}  // namespace spinneret::config
