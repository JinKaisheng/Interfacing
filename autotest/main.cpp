#include "../interface.h"
#include <iostream>
#include <dlfcn.h>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <library_path>" << std::endl;
        std::cerr << "Example: " << argv[0] << " ./lib/libimpl_a.so" << std::endl;
        return 1;
    }
    
    // 加载动态库
    void* handle = dlopen(argv[1], RTLD_LAZY);
    if (!handle) {
        std::cerr << "Failed to load library: " << dlerror() << std::endl;
        return 1;
    }
    
    // 获取工厂函数
    CreateInterfaceFunc createFunc = (CreateInterfaceFunc)dlsym(handle, "create_instance");
    if (!createFunc) {
        std::cerr << "Failed to find create_instance: " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }
    
    DestroyInterfaceFunc destroyFunc = (DestroyInterfaceFunc)dlsym(handle, "destroy_instance");
    if (!destroyFunc) {
        std::cerr << "Failed to find destroy_instance: " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }
    
    // 创建实例
    Interface* p = createFunc();
    if (!p) {
        std::cerr << "Failed to create instance" << std::endl;
        dlclose(handle);
        return 1;
    }
    
    // 调用接口方法
    p->print();
    p->foo();
    p->bar();
    
    // 销毁实例
    destroyFunc(p);
    
    // 关闭动态库
    dlclose(handle);
    
    return 0;
}