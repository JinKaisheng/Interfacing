#ifndef INTERFACE_H
#define INTERFACE_H

class Interface {
public:
    virtual ~Interface() = default;  // 虚析构函数
    virtual void print() = 0;
    virtual void foo() = 0;
    virtual void bar() = 0;
};

// 工厂函数类型定义
typedef Interface* (*CreateInterfaceFunc)();
typedef void (*DestroyInterfaceFunc)(Interface*);

#endif // INTERFACE_H