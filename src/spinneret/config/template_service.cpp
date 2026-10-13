#include "spinneret/config/template_service.h"

#include <algorithm>
#include <format>

#include "spinneret/util/files.h"
#include "spinneret/util/logging.h"

namespace spinneret::config {

namespace fs = std::filesystem;

namespace {
const util::Logger kLog{"config.templates"};
}

bool TemplateService::is_valid_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::vector<std::string> TemplateService::list() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(config_.dir() / "templates", ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string stem = entry.path().stem().string();
            if (is_valid_name(stem)) names.push_back(stem);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::set<std::string> TemplateService::apply(std::string_view name) {
    if (!is_valid_name(name)) {
        throw ConfigError(ConfigError::Kind::Invalid, std::format("Invalid template name '{}'", name));
    }

    fs::path path = config_.dir() / "templates" / (std::string(name) + ".json");
    auto text = util::read_file(path);
    if (!text) throw ConfigError(ConfigError::Kind::NotFound, std::format("Unknown template '{}'", name));

    auto properties = nlohmann::json::parse(*text, nullptr, false);
    if (properties.is_discarded()) {
        throw ConfigError(ConfigError::Kind::Invalid, std::format("Template '{}' is not valid JSON", name));
    }

    auto changed = config_.update(properties);
    kLog.info("Applied template '{}' ({} properties)", name, properties.size());
    return changed;
}

}  // namespace spinneret::config
