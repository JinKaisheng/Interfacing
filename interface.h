#ifndef INTERFACING_INTERFACE_H
#define INTERFACING_INTERFACE_H

#include <higgsIS/ClassLoader.h>

#include <string>

#define INTERFACE_VERSION "1.0.0"

class Interface : public HiggsIS::Loadable {
public:
    ~Interface() override = default;

    virtual void print() = 0;
    virtual void foo() = 0;
    virtual void bar() = 0;
    virtual std::string GetVersion() = 0;
};

#endif