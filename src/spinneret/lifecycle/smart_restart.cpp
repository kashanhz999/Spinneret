#include "spinneret/lifecycle/smart_restart.h"

#include "spinneret/util/logging.h"
#include "spinneret/util/strings.h"

namespace spinneret::lifecycle {

namespace {

const util::Logger kLog{"lifecycle"};

bool any_starts_with(const std::set<std::string>& keys, std::string_view prefix) {
    for (const auto& key : keys) {
        if (util::starts_with(key, prefix)) return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> SmartRestart::handle_config_change(const std::set<std::string>& changed_keys) {
    std::vector<std::string> touched;
    if (changed_keys.empty()) return touched;

    const bool mq_connection = any_starts_with(changed_keys, "ibm.mq.");

    auto restart_input = [&](const std::string& name) {
        auto* adapter = registry_.find_input(name);
        if (!adapter) return;
        kLog.info("Restarting {}", name);
        adapter->stop();
        auto result = adapter->start();
        kLog.info("{} restart result: {}", name, ports::to_string(result));
        touched.push_back(name);
    };
    auto reset_output = [&](const std::string& name) {
        auto* output = registry_.find_output(name);
        if (!output) return;
        kLog.info("Resetting {}", name);
        output->reset();
        touched.push_back(name);
    };

    if (mq_connection || any_starts_with(changed_keys, "adapters.input.ibmmq.")) restart_input("IbmMqInput");
    if (any_starts_with(changed_keys, "adapters.input.rest.")) restart_input("RestInput");
    if (mq_connection || any_starts_with(changed_keys, "adapters.output.ibmmq.")) reset_output("IbmMqOutput");
    if (any_starts_with(changed_keys, "adapters.output.rest.")) reset_output("RestOutput");

    return touched;
}

}  // namespace spinneret::lifecycle
