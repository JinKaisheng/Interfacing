#include "interface.h"

#include <higgsops/ConfigFactory.h>

#include <iostream>
#include <string>
#include <utility>

class ImplB final : public Interface {
    public:
        explicit ImplB(std::string message)
            : message_(std::move(message)) {}
    
        void print() override {
            std::cout << "Implementation B [" << message_
                      << "] - print()" << std::endl;
        }
        void foo() override {
            std::cout << "Implementation B - foo()" << std::endl;
        }
        void bar() override {
            std::cout << "Implementation B - bar()" << std::endl;
        }
        std::string GetVersion() override {
            return INTERFACE_VERSION;
        }
    
        static HiggsIS::Loadable* NewInstance(const char* config_token);
    
    private:
        std::string message_;
    };
    
    HiggsIS::Loadable* ImplB::NewInstance(const char* config_token) {
        const higgsops::config::Map config =
            higgsops::GetAssignedConfig(config_token);
        return new ImplB(config.GetOrDefault("message", "default-B"));
    }
    
    HCL_SO_VERSION(INTERFACE_VERSION)