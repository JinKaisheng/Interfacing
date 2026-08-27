#include "impl_b.h"

#include <iostream>
#include <utility>

ImplB::ImplB(std::string message) : message_(std::move(message)) {}

void ImplB::print() {
    std::cout << "Implementation B [" << message_ << "] - print()"
              << std::endl;
}

void ImplB::foo() {
    std::cout << "Implementation B - foo()" << std::endl;
}

void ImplB::bar() {
    std::cout << "Implementation B - bar()" << std::endl;
}

std::string ImplB::GetVersion() {
    return INTERFACE_VERSION;
}

HiggsIS::Loadable* ImplB::NewInstance(const char* config_token) {
    return NewInstance(higgsops::GetAssignedConfig(config_token));
}

HiggsIS::Loadable* ImplB::NewInstance(
    const higgsops::config::Map& config) {
    return new ImplB(config.GetOrDefault("message", "default-B"));
}
