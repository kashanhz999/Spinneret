#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "spinneret/config/config_service.h"

namespace spinneret::config {

/// Predefined configuration bundles in `<configDir>/templates/<name>.json`.
class TemplateService {
public:
    explicit TemplateService(ConfigService& config) : config_(config) {}

    std::vector<std::string> list() const;

    /// Applies a template as a single config update. Names are restricted to
    /// `[A-Za-z0-9_-]` so they cannot escape the templates directory.
    /// Returns the keys whose effective value changed.
    std::set<std::string> apply(std::string_view name);

    static bool is_valid_name(std::string_view name);

private:
    ConfigService& config_;
};

}  // namespace spinneret::config
