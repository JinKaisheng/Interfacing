#include "interface.h"

#include <higgsops/ConfigFactory.h>

#include <iostream>
#include <string>
#include <utility>

class ImplA final : public Interface {
public:
    explicit ImplA(std::string message)
        : message_(std::move(message)) {}

    void print() override {
        std::cout << "Implementation A [" << message_
                  << "] - print()" << std::endl;
    }

    void foo() override {
        std::cout << "Implementation A - foo()" << std::endl;
    }

    void bar() override {
        std::cout << "Implementation A - bar()" << std::endl;
    }

    std::string GetVersion() override {
        return INTERFACE_VERSION;
    }

    static HiggsIS::Loadable* NewInstance(const char* config_token);

private:
    std::string message_;
};

// 若直接在类体内定义，函数天然是 inline；由于普通代码没有调用 点，优化器可能不把它导出到动态符号表，运行时 ClassLoader 就找不到它。
HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    const higgsops::config::Map config =
        higgsops::GetAssignedConfig(config_token);
    return new ImplA(config.GetOrDefault("message", "default-A"));
}

// YAML 中写的动态库版本 是否等于 .so 文件自己声明的动态库版本
HCL_SO_VERSION(INTERFACE_VERSION)