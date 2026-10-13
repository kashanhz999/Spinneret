#include "spinneret/security/api_key_service.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "spinneret/util/files.h"
#include "spinneret/util/logging.h"
#include "spinneret/util/random.h"
#include "spinneret/util/sha256.h"

namespace spinneret::security {

namespace {

const util::Logger kLog{"security.apikeys"};

constexpr std::size_t kIdLength = 12;
constexpr std::size_t kVisiblePrefix = 8;

}  // namespace

ApiKeyService::ApiKeyService(std::filesystem::path file) : file_(std::move(file)) {}

void ApiKeyService::load() {
    auto text = util::read_file(file_);
    std::lock_guard lock(mutex_);
    by_hash_.clear();
    if (!text) return;

    auto root = nlohmann::json::parse(*text, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("keys") || !root["keys"].is_array()) {
        throw std::runtime_error(file_.string() + " is not a valid Spinneret key file");
    }

    for (const auto& k : root["keys"]) {
        Entry entry;
        entry.sha256 = k.value("sha256", "");
        if (entry.sha256.size() != 64) {
            throw std::runtime_error(file_.string() + " contains a key without a valid sha256");
        }
        entry.info.id = entry.sha256.substr(0, kIdLength);
        entry.info.name = k.value("name", "");
        entry.info.prefix = k.value("prefix", "");
        entry.info.active = k.value("active", true);
        entry.info.created_at =
            util::parse_iso8601(k.value("createdAt", "")).value_or(util::Clock::time_point{});
        by_hash_[entry.sha256] = std::move(entry);
    }
    kLog.info("Loaded {} API key(s)", by_hash_.size());
}

void ApiKeyService::save_locked() const {
    nlohmann::ordered_json root;
    root["version"] = 1;
    root["keys"] = nlohmann::ordered_json::array();
    for (const auto& [hash, entry] : by_hash_) {
        root["keys"].push_back({
            {"name", entry.info.name},
            {"prefix", entry.info.prefix},
            {"sha256", hash},
            {"createdAt", util::iso8601_utc(entry.info.created_at)},
            {"active", entry.info.active},
        });
    }
    util::atomic_write(file_, root.dump(2) + "\n", /*owner_only=*/true);
}

std::string ApiKeyService::create(std::string name) {
    std::string key = std::string(kKeyPrefix) + util::random_hex(32);
    Entry entry;
    entry.sha256 = util::sha256_hex(key);
    entry.info = ApiKeyInfo{
        .id = entry.sha256.substr(0, kIdLength),
        .name = std::move(name),
        .prefix = key.substr(0, kVisiblePrefix),
        .created_at = util::Clock::now(),
        .active = true,
    };

    std::lock_guard lock(mutex_);
    by_hash_[entry.sha256] = entry;
    try {
        save_locked();
    } catch (...) {
        by_hash_.erase(entry.sha256);
        throw;
    }
    kLog.info("Created API key '{}' (id {})", entry.info.name, entry.info.id);
    return key;
}

std::optional<std::string> ApiKeyService::ensure_bootstrap_key() {
    {
        std::lock_guard lock(mutex_);
        if (!by_hash_.empty()) return std::nullopt;
    }
    return create("Default Admin");
}

bool ApiKeyService::is_valid(std::string_view key) const {
    if (key.empty()) return false;
    const std::string hash = util::sha256_hex(key);
    std::lock_guard lock(mutex_);
    auto it = by_hash_.find(hash);
    return it != by_hash_.end() && it->second.info.active;
}

bool ApiKeyService::revoke(std::string_view id) {
    std::lock_guard lock(mutex_);
    for (auto it = by_hash_.begin(); it != by_hash_.end(); ++it) {
        if (it->second.info.id == id) {
            Entry removed = it->second;
            by_hash_.erase(it);
            try {
                save_locked();
            } catch (...) {
                by_hash_[removed.sha256] = removed;
                throw;
            }
            kLog.info("Revoked API key '{}' (id {})", removed.info.name, removed.info.id);
            return true;
        }
    }
    return false;
}

std::vector<ApiKeyInfo> ApiKeyService::list() const {
    std::lock_guard lock(mutex_);
    std::vector<ApiKeyInfo> keys;
    for (const auto& [hash, entry] : by_hash_) keys.push_back(entry.info);
    return keys;
}

}  // namespace spinneret::security
