#ifndef INTERFACING_INTERFACE_LOADER_H
#define INTERFACING_INTERFACE_LOADER_H

#include "interface.h"

#include <memory>
#include <string>

bool IsInterfaceVersionCompatible(const std::string& required,
                                  const std::string& implementation);

void ValidateInterfaceVersion(Interface& instance);

std::unique_ptr<Interface> LoadInterfaceFromConfig(
    const std::string& config_file,
    bool validate_version = true);

#endif