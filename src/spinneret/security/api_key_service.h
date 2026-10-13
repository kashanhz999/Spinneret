#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "spinneret/util/time.h"

namespace spinneret::security {

struct ApiKeyInfo {
    std::string id;      ///< First 12 hex chars of the key's SHA-256; used to revoke.
    std::string name;
    std::string prefix;  ///< First characters of the key, to help people recognise it.
    util::Clock::time_point created_at;
    bool active = true;
};

/// API keys stored in `<configDir>/api-keys.json`.
///
/// Only SHA-256 hashes are written to disk, so the file does not reveal usable
/// keys. A key is shown in plain text exactly once: when it is created.
class ApiKeyService {
public:
    static constexpr const char* kKeyPrefix = "spn_";

    explicit ApiKeyService(std::filesystem::path file);

    /// Loads the key file. Throws std::runtime_error if it is corrupt.
    void load();

    /// When no keys exist, creates one named "Default Admin" and returns it.
    std::optional<std::string> ensure_bootstrap_key();

    /// Creates and persists a key, returning its plain-text value.
    std::string create(std::string name);

    bool is_valid(std::string_view key) const;

    /// Revokes the key with this id. Returns false if there is no such key.
    bool revoke(std::string_view id);

    std::vector<ApiKeyInfo> list() const;

private:
    struct Entry {
        ApiKeyInfo info;
        std::string sha256;
    };

    void save_locked() const;

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> by_hash_;
};

}  // namespace spinneret::security
