#ifndef INTERFACING_INTERFACE_REGISTRY_H
#define INTERFACING_INTERFACE_REGISTRY_H

#include "interface.h"

#include <higgsops/ConfigFactory.h>

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

// Static loading does not cross a shared-library ABI boundary, so it can pass
// the already-parsed Map directly. Dynamic loading keeps the HCL const char*
// token ABI and is handled separately by HiggsIS::ClassLoader.
using StaticInterfaceFactory =
    HiggsIS::Loadable* (*)(const higgsops::config::Map& config);

class InterfaceRegistry {
public:
    template <typename T>
    void Register(std::string class_name) {
        static_assert(std::is_base_of<Interface, T>::value,
                      "A statically registered type must derive from Interface");

        // T has two NewInstance overloads. The explicit function-pointer type
        // selects the Map overload used by the in-process static registry.
        const StaticInterfaceFactory factory =
            static_cast<StaticInterfaceFactory>(&T::NewInstance);
        RegisterFactory(std::move(class_name), factory);
    }

    bool Contains(const std::string& class_name) const noexcept;

    std::unique_ptr<Interface> Create(
        const std::string& class_name,
        const higgsops::config::Map& config) const;

private:
    void RegisterFactory(std::string class_name,
                         StaticInterfaceFactory factory);

    std::map<std::string, StaticInterfaceFactory> factories_;
};

#endif  // INTERFACING_INTERFACE_REGISTRY_H
