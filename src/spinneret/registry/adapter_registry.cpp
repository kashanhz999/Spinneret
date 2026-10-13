#include "spinneret/registry/adapter_registry.h"

namespace spinneret::registry {

void AdapterRegistry::register_input(ports::InputAdapter& adapter) {
    std::lock_guard lock(mutex_);
    inputs_[adapter.name()] = &adapter;
}

void AdapterRegistry::register_output(ports::OutputPublisher& publisher) {
    std::lock_guard lock(mutex_);
    outputs_[publisher.name()] = &publisher;
}

ports::InputAdapter* AdapterRegistry::find_input(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = inputs_.find(name);
    return it == inputs_.end() ? nullptr : it->second;
}

ports::OutputPublisher* AdapterRegistry::find_output(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = outputs_.find(name);
    return it == outputs_.end() ? nullptr : it->second;
}

std::vector<ports::InputAdapter*> AdapterRegistry::inputs() const {
    std::lock_guard lock(mutex_);
    std::vector<ports::InputAdapter*> list;
    for (const auto& [name, adapter] : inputs_) list.push_back(adapter);
    return list;
}

std::vector<ports::OutputPublisher*> AdapterRegistry::outputs() const {
    std::lock_guard lock(mutex_);
    std::vector<ports::OutputPublisher*> list;
    for (const auto& [name, publisher] : outputs_) list.push_back(publisher);
    return list;
}

}  // namespace spinneret::registry
