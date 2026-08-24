# Interfacing - Interface-based Plugin Architecture

基于 Interface 的插件化架构示例，展示如何使用 C++ 动态库实现解耦设计。

## 项目结构
- `interface.h`: 定义统一接口
- `impl_a/`: 实现 A（编译为 libimpl_a.so）
- `impl_b/`: 实现 B（编译为 libimpl_b.so）
- `autotest/`: 测试程序，动态加载实现

## 编译
```bash
mkdir build && cd build
cmake ..
make