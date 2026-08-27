#ifndef INTERFACING_IMPL_A_H
#define INTERFACING_IMPL_A_H

#include "interface.h"

#include <higgsops/ConfigFactory.h>

#include <string>

class ImplA final : public Interface {
public:
    explicit ImplA(std::string message);

    void print() override;
    void foo() override;
    void bar() override;
    std::string GetVersion() override;

    // Dynamic HCL factory: token crosses the shared-library ABI boundary.
    static HiggsIS::Loadable* NewInstance(const char* config_token);

    // Static registry factory: the parsed configuration stays in-process.
    static HiggsIS::Loadable* NewInstance(
        const higgsops::config::Map& config);

private:
    std::string message_;
};

#endif  // INTERFACING_IMPL_A_H
