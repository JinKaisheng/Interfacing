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

    // 普通成员函数需要先有对象才能调用，但现在的目标正是创建第一个对象。静态成员函数 不需要 this，可以直接作为工厂入口
    static HiggsIS::Loadable* NewInstance(const char* config_token);

private:
    std::string message_;
};

// 若直接在类体内定义，函数天然是 inline；如果函数是 inline 的，并且在工程源码中没有显式调用点，
// 编译器可能会优化掉这个函数，不生成独立的符号，运行时 ClassLoader 就找不到它。
HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    const higgsops::config::Map config =
        higgsops::GetAssignedConfig(config_token);
    return new ImplA(config.GetOrDefault("message", "default-A"));
}

// YAML 中写的动态库版本 是否等于 .so 文件自己声明的动态库版本
HCL_SO_VERSION(INTERFACE_VERSION)

/*
    宏概念上相当于导出：
    extern "C" const char* HCL_DynamicLibVersion() {
        return "1.0.0";
    }
*/
