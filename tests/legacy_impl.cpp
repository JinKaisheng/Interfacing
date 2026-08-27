#include "interface.h"
#include <higgsops/ConfigFactory.h>
#include <iostream>

class LegacyImpl final : public Interface {
public:
    void print() override {
        std::cout << "Legacy implementation - print()" << std::endl;
    }
    void foo() override {
        std::cout << "Legacy implementation - foo()" << std::endl;
    }
    void bar() override {
        std::cout << "Legacy implementation has old bar() semantics"
                  << std::endl;
    }
    std::string GetVersion() override {
        return "0.9.0";
    }

    static HiggsIS::Loadable* NewInstance(const char* config_token);
};

HiggsIS::Loadable* LegacyImpl::NewInstance(const char* config_token) {
    (void)higgsops::GetAssignedConfig(config_token);
    return new LegacyImpl();
}

// CMake also uses this product-version value when generating legacy.yaml.
HCL_SO_VERSION(INTERFACING_LEGACY_PLUGIN_VERSION)
