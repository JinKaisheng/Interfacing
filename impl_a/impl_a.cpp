#include "impl_a.h"

#include <iostream>
#include <utility>

ImplA::ImplA(std::string message) : message_(std::move(message)) {}

void ImplA::print() {
    std::cout << "Implementation A [" << message_ << "] - print()"
              << std::endl;
}

void ImplA::foo() {
    std::cout << "Implementation A - foo()" << std::endl;
}

void ImplA::bar() {
    std::cout << "Implementation A - bar()" << std::endl;
}

std::string ImplA::GetVersion() {
    return INTERFACE_VERSION;
}

HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    // Dynamic path: turn the stable const char* token ABI back into a Map,
    // then delegate to the same constructor path used by static loading.
    return NewInstance(higgsops::GetAssignedConfig(config_token));
}

HiggsIS::Loadable* ImplA::NewInstance(
    const higgsops::config::Map& config) {
    // Static path calls this overload directly. Both modes therefore share
    // defaults, validation and object construction behavior.
    return new ImplA(config.GetOrDefault("message", "default-A"));
}
