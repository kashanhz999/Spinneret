#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "spinneret/ports/input_adapter.h"
#include "spinneret/ports/output_publisher.h"

namespace spinneret::registry {

/// Name → adapter lookup for lifecycle management and fan-out. Does not own
/// the adapters. Iteration order is by name, so it is deterministic.
class AdapterRegistry {
public:
    void register_input(ports::InputAdapter& adapter);
    void register_output(ports::OutputPublisher& publisher);

    ports::InputAdapter* find_input(const std::string& name) const;
    ports::OutputPublisher* find_output(const std::string& name) const;

    std::vector<ports::InputAdapter*> inputs() const;
    std::vector<ports::OutputPublisher*> outputs() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, ports::InputAdapter*> inputs_;
    std::map<std::string, ports::OutputPublisher*> outputs_;
};

}  // namespace spinneret::registry
