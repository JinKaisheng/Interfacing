#ifndef INTERFACING_IMPL_B_H
#define INTERFACING_IMPL_B_H

#include "interface.h"

#include <higgsops/ConfigFactory.h>

#include <string>

class ImplB final : public Interface {
public:
    explicit ImplB(std::string message);

    void print() override;
    void foo() override;
    void bar() override;
    std::string GetVersion() override;

    static HiggsIS::Loadable* NewInstance(const char* config_token);
    static HiggsIS::Loadable* NewInstance(
        const higgsops::config::Map& config);

private:
    std::string message_;
};

#endif  // INTERFACING_IMPL_B_H
