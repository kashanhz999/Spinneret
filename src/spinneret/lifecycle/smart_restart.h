#pragma once

#include <set>
#include <string>
#include <vector>

#include "spinneret/registry/adapter_registry.h"

namespace spinneret::lifecycle {

/// Applies a configuration change to the adapters it affects:
///
///  - `adapters.input.<x>.*`   → restart that input (start() re-checks `enabled`)
///  - `adapters.output.<x>.*`  → reset that output (drops cached connections)
///  - `ibm.mq.*`               → restart IbmMqInput and reset IbmMqOutput, so new
///                               connection settings take effect
class SmartRestart {
public:
    explicit SmartRestart(registry::AdapterRegistry& registry) : registry_(registry) {}

    /// Returns the names of the adapters that were restarted or reset.
    std::vector<std::string> handle_config_change(const std::set<std::string>& changed_keys);

private:
    registry::AdapterRegistry& registry_;
};

}  // namespace spinneret::lifecycle
