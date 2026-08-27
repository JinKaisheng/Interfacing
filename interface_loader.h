#ifndef INTERFACING_INTERFACE_LOADER_H
#define INTERFACING_INTERFACE_LOADER_H

#include "interface.h"
#include "interface_registry.h"

#include <higgsops/ConfigFactory.h>

#include <memory>
#include <string>
#include <utility>

bool IsInterfaceVersionCompatible(const std::string& required,
                                  const std::string& implementation);

void ValidateInterfaceVersion(Interface& instance);

enum class LoadMode {
    Static,
    Dynamic,
};

const char* ToString(LoadMode mode) noexcept;

// Keep the object and the path used to obtain it together. Tests can assert
// mode directly instead of inferring it from log text or object behavior.
struct LoadedInterface {
    std::unique_ptr<Interface> instance;
    LoadMode mode;
    std::string class_name;
};

class HybridInterfaceLoader {
public:
    HybridInterfaceLoader();

    template <typename T>
    void RegisterStatic(std::string class_name) {
        registry_.Register<T>(std::move(class_name));
    }

    LoadedInterface LoadFromConfig(
        const std::string& config_file,
        bool validate_version = true) const;

    LoadedInterface Load(
        const higgsops::config::Map& config,
        bool validate_version = true) const;

private:
    InterfaceRegistry registry_;
};

LoadedInterface LoadInterfaceWithModeFromConfig(
    const std::string& config_file,
    bool validate_version = true);

#endif
