#include "../interface.h"
#include <iostream>

class ImplB : public Interface {
    void print() {
        std::cout << "This is Implementation B - print()" << std::endl;  
    }
    void foo() {
        std::cout << "This is Implementation B - foo()" << std::endl;  
    }
    void bar() {
        std::cout << "This is Implementation B - bar()" << std::endl;  
    } 
};

extern "C" {
    Interface* create_instance() {
        return new ImplB();
    }

    void destroy_instance(Interface* p) {
        delete p;
    }
}