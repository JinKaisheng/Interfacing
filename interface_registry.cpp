#include "interface_registry.h"

#include <stdexcept>
#include <utility>

void InterfaceRegistry::RegisterFactory(
    std::string class_name,
    StaticInterfaceFactory factory) {
    if (class_name.empty()) {
        throw std::invalid_argument("Static class name must not be empty");
    }
    if (factory == nullptr) {
        throw std::invalid_argument("Static factory must not be null: " +
                                    class_name);
    }

    const auto inserted = factories_.emplace(std::move(class_name), factory);
    if (!inserted.second) {
        throw std::logic_error("Static class already registered: " +
                               inserted.first->first);
    }
}

bool InterfaceRegistry::Contains(
    const std::string& class_name) const noexcept {
    return factories_.find(class_name) != factories_.end();
}

std::unique_ptr<Interface> InterfaceRegistry::Create(
    const std::string& class_name,
    const higgsops::config::Map& config) const {
    const auto found = factories_.find(class_name);
    if (found == factories_.end()) {
        throw std::runtime_error("Static implementation is not registered: " +
                                 class_name);
    }

    // Own the base pointer immediately so an invalid dynamic_cast cannot leak
    // the object returned by a malformed factory.
    std::unique_ptr<HiggsIS::Loadable> raw(found->second(config));
    if (!raw) {
        throw std::runtime_error("Static factory returned null: " + class_name);
    }

    Interface* typed = dynamic_cast<Interface*>(raw.get());
    if (typed == nullptr) {
        throw std::runtime_error(
            "Static factory returned an object that does not implement Interface: " +
            class_name);
    }

    raw.release();
    return std::unique_ptr<Interface>(typed);
}
