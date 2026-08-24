#include "../interface.h"
#include <iostream>

class ImplA : public Interface {
public:
    void print() override {
        std::cout << "This is Implementation A - print()" << std::endl;
    }
    
    void foo() override {
        std::cout << "This is Implementation A - foo()" << std::endl;
    }
    
    void bar() override {
        std::cout << "This is Implementation A - bar()" << std::endl;
    }
};

// C 风格的工厂函数，用于创建和销毁对象
extern "C" {
    Interface* create_instance() {
        return new ImplA();
    }
    
    void destroy_instance(Interface* p) {
        delete p;
    }
}