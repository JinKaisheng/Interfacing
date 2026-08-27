# ClassLoader 与 ConfigFactory 调用链详解

> 构建目录说明：本文部分命令保留基础版/早期进阶阶段的 `build/server`，用于还原
> 当时的学习路径。当前高阶版正式使用 `build/debug` 和 `build/advanced`；实际操作
> 请以 `README.md` 和 `docs/DEVELOPMENT_COMMAND_REFERENCE.md` 为准。

本文用于解释下面两遍学习步骤背后的真实代码链路：

```text
第二遍：
只接依赖 -> 阅读 ClassLoader.h 和 ConfigFactory.h

第三遍：
只改一个 ImplA -> 检查 HCL/NewInstance 符号 -> 成功加载 A
```

目标不是背诵 API，而是能够沿着一次真实运行回答：配置在哪里解析、动态库在哪里
打开、符号怎样找到、配置怎样进入插件、对象怎样转换成 Interface，以及为什么要先
检查两个动态符号再运行。

---

## 1. 一张图看懂完整调用链

```text
main.cpp
  ↓
LoadInterfaceWithModeFromConfig("impl_a.yaml")
  ↓
HybridInterfaceLoader::LoadFromConfig()
  ↓
HybridInterfaceLoader::Load()
  ↓
higgsops::config::LoadConfigFile()
  ↓
higgsops::LoadClass<Interface>()
  ↓
HiggsIS::ClassLoader<Interface>
  ├─ 打开 libimpl_a.so
  ├─ 查找 HCL_DynamicLibVersion
  ├─ 校验 YAML 声明的 HCL 版本
  └─ 查找 ImplA::NewInstance(char const*)
        ↓
LoaderHelper 暂存 YAML 并生成 token
        ↓
ImplA::NewInstance(token)
        ↓
higgsops::GetAssignedConfig(token)
        ↓
new ImplA("configured-A")
        ↓
HiggsIS::Loadable*
        ↓ dynamic_cast
Interface*
        ↓
std::unique_ptr<Interface>
        ↓
ValidateInterfaceVersion()
        ↓
instance->print()/foo()/bar()
```

五个核心角色：

| 角色 | 职责 |
| --- | --- |
| `ConfigFactory` | 解析配置，组织“按配置加载类”的上层流程 |
| `ClassLoader` | 打开 `.so`、验证 HCL 版本、查找工厂、创建并校验对象 |
| `HCL_SO_VERSION` | 让动态库导出固定的版本符号 |
| `NewInstance` | 插件统一的对象创建入口 |
| `Interface` | 主程序与具体实现之间共享的抽象 ABI |

---

## 2. 为什么第二遍只接依赖、先读头文件

第二遍不急着改 `ImplA`，而是先回答框架契约：

1. Interface 必须继承谁？
2. 工厂函数叫什么、参数和返回值是什么？
3. YAML 的字段名是什么？
4. 版本怎样从 `.so` 暴露？
5. ClassLoader 最终怎样把基类指针变成目标接口指针？

服务器恢复依赖后阅读：

```bash
sed -n '35,120p' install/include/higgsIS/ClassLoader.h
sed -n '300,355p' install/include/higgsops/ConfigFactory.h
```

不要只看旧注释。HiggsOps.Interface 2.36.0 的真实代码读取：

```cpp
classInfo["file"].AsString();
classInfo["ver"].AsString();
classInfo["class"].AsString();
```

因此 YAML 使用 `class.class`，不是某些旧注释里的 `class.name`。

---

## 3. YAML 的两类信息

CMake 生成的 `impl_a.yaml` 大致是：

```yaml
class:
  file: /home/jinkaisheng/InterfacingJin/Interfacing/build/server/lib/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-A
```

这里同时包含基础设施配置和业务配置：

```text
根配置
├── class                    ClassLoader使用
│   ├── file                 动态库真实路径
│   ├── ver                  动态库声明版本
│   └── class                C++完整类名
└── message                  ImplA使用的业务字段
```

字段含义：

| 字段 | 使用者 | 含义 |
| --- | --- | --- |
| `class.file` | ClassLoader | 打开哪个 `.so` |
| `class.ver` | ClassLoader | 期望 `.so` 通过 HCL 声明什么版本 |
| `class.class` | ClassLoader | 在 `.so` 中寻找哪个类的 `NewInstance` |
| `message` | ImplA | 创建对象时使用的业务数据 |

如果类位于命名空间中：

```cpp
namespace demo {
class ImplA final : public Interface { /* ... */ };
}
```

配置必须写完整名称：

```yaml
class: demo::ImplA
```

---

## 4. 项目统一入口 `LoadInterfaceWithModeFromConfig`

项目代码：

```cpp
LoadedInterface LoadInterfaceWithModeFromConfig(
    const std::string& config_file,
    bool validate_version)
{
    HybridInterfaceLoader loader;
    return loader.LoadFromConfig(config_file, validate_version);
}

LoadedInterface HybridInterfaceLoader::LoadFromConfig(
    const std::string& config_file,
    bool validate_version) const
{
    const higgsops::config::Node root =
        higgsops::config::LoadConfigFile(config_file);
    return Load(root.AsMap(), validate_version);
}
```

第一层是整个程序唯一的文件加载入口；第二层只负责把 YAML 文件变成 `Map`，再把
`Map` 交给统一分派函数 `HybridInterfaceLoader::Load()`。分派函数根据配置选择静态
注册表或 Higgs 动态加载，并最终返回 `LoadedInterface`。因此调用方同时得到对象、
真实加载模式和类名，不需要从日志或行为猜测路径。

### 4.1 读取文件

```cpp
higgsops::config::LoadConfigFile(config_file)
```

把 YAML 解析成 `config::Node`。假设输入是 `impl_a.yaml`，解析后的逻辑结构为：

```text
root["class"]["file"]  = "/.../libimpl_a.so"
root["class"]["ver"]   = "1.0.0"
root["class"]["class"] = "ImplA"
root["message"]         = "configured-A"
```

### 4.2 转成 Map

```cpp
root.AsMap()
```

表示调用方确认根节点是键值映射，并希望按字符串键访问它。

### 4.3 模板参数

```cpp
higgsops::LoadClass<Interface>(root.AsMap())
```

模板参数是 `Interface`，因此整个调用要求最终对象可以转换为 `Interface*`。上层
代码在编译时不认识 `ImplA`，选择具体类的工作交给 YAML 和 ClassLoader。

---

## 5. `ConfigFactory.h` 中的 `LoadClass`

HiggsOps.Interface 2.36.0 的核心代码：

```cpp
template<typename T>
std::unique_ptr<T> LoadClass(const config::Map& configNode)
{
    const config::Map& classInfo =
        configNode["class"].AsMap();

    HiggsIS::ClassLoader<T> loader(
        classInfo["file"].AsString(),
        classInfo["ver"].AsString(),
        classInfo["class"].AsString());

    __details::LoaderHelper helper(configNode);
    return loader.NewInstance(helper.token.data());
}
```

对于本项目，模板实例化后可近似阅读为：

```cpp
std::unique_ptr<Interface> LoadClass(const config::Map& configNode)
{
    const config::Map& classInfo =
        configNode["class"].AsMap();

    HiggsIS::ClassLoader<Interface> loader(
        "/.../libimpl_a.so",
        "1.0.0",
        "ImplA");

    LoaderHelper helper(configNode);
    return loader.NewInstance(helper.token.data());
}
```

调用顺序不能颠倒：先创建 ClassLoader 并验证动态库入口，再注册业务配置并调用工厂。

---

## 6. ClassLoader 构造阶段

ClassLoader 内部实现可以概念化为：

```cpp
template<typename T>
class ClassLoader {
public:
    ClassLoader(const std::string& file,
                const std::string& expectedVersion,
                const std::string& className) {
        handle = OpenDynamicLibrary(file);
        ValidateHclVersion(handle, expectedVersion);
        factory = FindNewInstance(handle, className);
    }
};
```

### 6.1 打开 `.so`

Linux 底层概念上使用：

```cpp
dlopen("/.../libimpl_a.so", RTLD_NOW);
```

此处可能因为以下原因失败：

- `class.file` 路径不存在；
- `.so` 架构与系统不匹配；
- `.so` 的间接依赖不存在；
- RPATH 或 `LD_LIBRARY_PATH` 不正确；
- 文件不是有效 Linux ELF 动态库。

先检查：

```bash
file build/server/lib/libimpl_a.so
ldd build/server/lib/libimpl_a.so
```

### 6.2 查找 HCL 版本入口

ClassLoader 查找固定符号：

```text
HCL_DynamicLibVersion
```

它来自插件源码末尾：

```cpp
HCL_SO_VERSION(INTERFACE_VERSION)
```

当：

```cpp
#define INTERFACE_VERSION "1.0.0"
```

宏概念上相当于导出：

```cpp
extern "C" const char* HCL_DynamicLibVersion() {
    return "1.0.0";
}
```

实际宏展开形式以 `ClassLoader.h` 为准，但作用相同：生成名字固定的动态符号，使
ClassLoader 可以在不认识插件 C++ 类型的情况下读取版本。

ClassLoader 比较：

```text
YAML class.ver          = 1.0.0
.so HCL_SO_VERSION      = 1.0.0
```

不一致时在对象创建之前失败。

### 6.3 查找 `NewInstance`

YAML 给出：

```yaml
class: ImplA
```

ClassLoader 据此查找：

```cpp
ImplA::NewInstance(char const*)
```

Linux GCC 使用 C++ 名字修饰后，符号可能类似：

```text
_ZN5ImplA11NewInstanceEPKc
```

使用 `c++filt` 后恢复成人类可读形式：

```text
ImplA::NewInstance(char const*)
```

静态成员函数没有隐含的 `this` 参数，因此找到地址后可以按统一工厂签名调用：

```cpp
using Factory = HiggsIS::Loadable* (*)(const char*);
```

---

## 7. 为什么 Interface 继承 `HiggsIS::Loadable`

继承关系：

```text
HiggsIS::Loadable
        ↑
    Interface
        ↑
      ImplA
```

统一工厂返回：

```cpp
HiggsIS::Loadable*
```

而 `LoadClass<Interface>` 需要：

```cpp
Interface*
```

ClassLoader 可以在运行时验证：

```cpp
HiggsIS::Loadable* base = factory(token);
Interface* result = dynamic_cast<Interface*>(base);
```

如果插件返回的对象不是 `Interface` 实现，`dynamic_cast` 得到空指针，ClassLoader
即可拒绝错误类型。若 `Interface` 不继承 `Loadable`，这条统一转换链无法成立。

Interface 还必须有虚析构函数：

```cpp
virtual ~Interface() = default;
```

这样 `std::unique_ptr<Interface>` 删除一个实际为 `ImplA` 的对象时，才能正确调用
`ImplA` 的析构过程。

---

## 8. LoaderHelper 和 token

`ConfigFactory.h` 声明：

```cpp
class LoaderHelper {
public:
    HiggsIS::CharArray<64> token;
    LoaderHelper(const config::Map& node) noexcept;
    ~LoaderHelper() noexcept;
};
```

可以把它理解成临时配置寄存器：

```cpp
LoaderHelper::LoaderHelper(const Map& config) {
    token = GenerateToken();
    GlobalConfigRegistry[token] = config;
}

LoaderHelper::~LoaderHelper() {
    GlobalConfigRegistry.erase(token);
}
```

假设生成：

```text
token = "f74b12..."
```

注册表暂时保存：

```text
"f74b12..." → 整个 impl_a.yaml 配置节点
```

ClassLoader 的工厂 ABI 固定为：

```cpp
Loadable* NewInstance(const char* token);
```

这样无论插件需要 `message`、`host`、`port` 还是账户配置，工厂签名都不变。插件
应在 `NewInstance()` 调用期间立即用 token 领取配置，不应长期保存 token。

---

## 9. 第三遍为什么只改一个 ImplA

动态加载可能在很多层次失败：

```text
依赖恢复
  -> 编译
  -> 链接
  -> .so间接依赖
  -> YAML路径
  -> HCL版本符号
  -> NewInstance符号
  -> 类名
  -> token配置
  -> dynamic_cast
  -> Interface版本
```

如果同时修改 A、B、主程序和测试，失败时难以定位。只实现 A，可以先建立最小闭环：

```text
一个 YAML
  -> 一个 .so
  -> 一个 HCL 版本符号
  -> 一个 NewInstance 工厂符号
  -> 一个 Interface 对象
  -> 一次 print()
```

闭环成功后，ImplB 只是重复同一协议，不再引入新的机制。

---

## 10. ImplA 代码逐段解释

```cpp
class ImplA final : public Interface {
public:
    explicit ImplA(std::string message)
        : message_(std::move(message)) {}

    void print() override;
    void foo() override;
    void bar() override;

    std::string GetVersion() override {
        return INTERFACE_VERSION;
    }

    static HiggsIS::Loadable*
    NewInstance(const char* config_token);

private:
    std::string message_;
};
```

### 10.1 `final`

```cpp
class ImplA final : public Interface
```

表示 `ImplA` 是最终实现，不允许继续派生。它不是 ClassLoader 强制要求，但能让
实现边界更清晰。

### 10.2 构造函数

```cpp
explicit ImplA(std::string message)
    : message_(std::move(message)) {}
```

调用 `new ImplA("configured-A")` 后，成员状态为：

```text
message_ = "configured-A"
```

`explicit` 避免意外隐式转换，`std::move` 把参数中的字符串资源移动给成员。

### 10.3 虚函数

主程序持有的是：

```cpp
Interface* instance;
```

调用：

```cpp
instance->print();
```

通过虚函数表实际进入：

```cpp
ImplA::print();
```

这使主程序不需要包含或认识 `ImplA`。

### 10.4 为什么 `NewInstance` 是 static

普通成员函数需要先有对象才能调用，但现在的目标正是创建第一个对象。静态成员函数
不需要 `this`，可以直接作为工厂入口：

```text
尚无 ImplA 对象
  -> 调用静态 ImplA::NewInstance(token)
  -> new ImplA(...)
  -> 得到第一个对象
```

### 10.5 为什么返回 `Loadable*`

固定返回类型是 ClassLoader 的统一 ABI：

```cpp
HiggsIS::Loadable* (*)(const char*);
```

实际返回：

```cpp
return new ImplA(...);
```

发生合法向上转换：

```text
ImplA* -> Interface* -> HiggsIS::Loadable*
```

ClassLoader 再依据模板参数执行运行时类型校验。

---

## 11. `NewInstance` 内部配置链

真实实现：

```cpp
HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    const higgsops::config::Map config =
        higgsops::GetAssignedConfig(config_token);

    return new ImplA(
        config.GetOrDefault("message", "default-A"));
}
```

一次实际调用：

```text
config_token = "f74b12..."
  ↓
GetAssignedConfig("f74b12...")
  ↓
取回整个YAML节点
  ↓
config.GetOrDefault("message", "default-A")
  ↓
得到 "configured-A"
  ↓
new ImplA("configured-A")
  ↓
返回 HiggsIS::Loadable*
```

如果 YAML 不包含 `message`，`GetOrDefault` 返回 `default-A`。

---

## 12. ClassLoader 调用工厂和异常转换

ClassLoader 中的核心片段：

```cpp
try {
    objPtr = factory(objectConfig.c_str());
}
catch (Exception& e1) {
    throw InstantiateException(typeid(T), e1);
}
catch (std::exception& e2) {
    throw InstantiateException(typeid(T), e2.what());
}
catch (...) {
    throw InstantiateException(typeid(T), "Unknown exception");
}
```

此时：

```text
factory      -> ImplA::NewInstance的函数地址
objectConfig -> token字符串
T            -> Interface
```

所以第一行近似等价于：

```cpp
HiggsIS::Loadable* objPtr =
    ImplA::NewInstance(token);
```

三层 `catch` 把插件内部不同异常统一翻译为 `InstantiateException`：

```text
HiggsIS::Exception ─┐
std::exception ─────┼─> InstantiateException(typeid(Interface), 原因)
其他异常 ───────────┘
```

这形成插件边界：上层不用了解每个实现内部使用的所有异常类型。

如果工厂只是返回空指针，它不会进入 `catch`，ClassLoader 还需要在后续执行空指针
和 `dynamic_cast` 结果检查。

---

## 13. 为什么 `NewInstance` 类内声明、类外定义

推荐写法：

```cpp
class ImplA final : public Interface {
public:
    static HiggsIS::Loadable*
    NewInstance(const char* token);
};

HiggsIS::Loadable*
ImplA::NewInstance(const char* token) {
    // ...
}
```

容易出错的写法：

```cpp
class ImplA final : public Interface {
public:
    static HiggsIS::Loadable* NewInstance(const char* token) {
        // 直接在类体内定义
    }
};
```

类体内定义的函数天然是 inline。工程源码没有正常调用：

```cpp
ImplA::NewInstance(token);
```

它只会在将来被 `dlsym` 按名字查找，而编译器看不到这个运行时调用点，因此可能不
生成可导出的动态符号。源码中“写了函数”不等于 `.so` 的动态符号表中“存在函数”。

类外定义能够促使编译器生成独立的外部符号。

---

## 14. 为什么检查两个动态符号

构建后先执行：

```bash
nm -D --defined-only build/server/lib/libimpl_a.so \
  | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

参数含义：

| 参数 | 作用 |
| --- | --- |
| `nm` | 查看 ELF 目标文件或 `.so` 的符号 |
| `-D` | 只查看动态符号表；`dlsym` 要找的就是这里 |
| `--defined-only` | 只显示该 `.so` 自己定义的符号 |
| `c++filt` | 把 C++ 修饰名还原成人类可读名称 |
| `grep` | 只保留两个关键入口 |

必须同时看到：

```text
HCL_DynamicLibVersion
ImplA::NewInstance(char const*)
```

含义：

| 结果 | 判断 |
| --- | --- |
| 两个都有 | 插件版本入口和对象工厂入口都已导出 |
| 只有 HCL 版本 | 常见原因是 `NewInstance` 类内定义，被优化掉 |
| 只有 NewInstance | 忘记写 `HCL_SO_VERSION` 或宏未生效 |
| 两个都没有 | 可能检查了旧 `.so`、错误目标或尚未重新构建 |

通过符号检查不代表一定能运行，还要检查动态库依赖：

```bash
ldd build/server/lib/libimpl_a.so | grep 'not found' || true
ldd build/server/bin/main.out | grep 'not found' || true
```

---

## 15. 从命令行完整跟踪一次 A

运行：

```bash
build/server/bin/main.out build/server/config/impl_a.yaml
```

逐步发生：

1. `main` 从 `argv[1]` 得到 YAML 路径；
2. `LoadInterfaceWithModeFromConfig` 创建统一 loader；
3. `HybridInterfaceLoader::LoadFromConfig` 读取 YAML；
4. `HybridInterfaceLoader::Load` 确认本次选择动态路径；
5. `LoadClass<Interface>` 取出 `file/ver/class`；
6. `ClassLoader<Interface>` 打开 `libimpl_a.so`；
7. 查找 `HCL_DynamicLibVersion`，比较 `1.0.0`；
8. 根据 `ImplA` 找到 `ImplA::NewInstance(char const*)`；
9. `LoaderHelper` 保存完整配置并生成 token；
10. ClassLoader 执行 `factory(token)`；
11. 实际进入 `ImplA::NewInstance(token)`；
12. `GetAssignedConfig(token)` 取回 YAML；
13. 读取 `message: configured-A`；
14. 执行 `new ImplA("configured-A")`；
15. 返回 `HiggsIS::Loadable*`；
16. ClassLoader 将其验证并转换为 `Interface*`；
17. 包装为 `std::unique_ptr<Interface>`；
18. 统一后处理返回 `LoadedInterface`；
19. 项目调用 `GetVersion()` 做第二层 Interface 版本校验；
20. `loaded.instance->print()` 通过虚函数表进入 `ImplA::print()`。

预期输出包含：

```text
Implementation A [configured-A] - print()
```

`configured-A` 的出现证明的不只是 `.so` 打开成功，而是下面整条配置链都成功：

```text
YAML -> LoaderHelper -> token -> GetAssignedConfig
     -> ImplA构造函数 -> message_ -> print()
```

---

## 16. 两层版本检查

### 16.1 HCL 动态库版本

```text
YAML class.ver
       对比
.so 的 HCL_SO_VERSION
```

回答：配置是否指向了自己声称的那个插件版本。

### 16.2 Interface 语义/ABI版本

```text
主程序的 INTERFACE_VERSION
       对比
instance->GetVersion()
```

回答：插件编译时使用的 Interface 是否与主程序需要的 Interface 兼容。

两者不是重复校验：

```text
HCL版本       检查配置声明与文件声明
Interface版本 检查调用双方的接口契约
```

---

## 17. 推荐的最小验证顺序

```bash
# 1. 依赖头文件
test -f install/include/higgsIS/ClassLoader.h
test -f install/include/higgsops/ConfigFactory.h

# 2. 依赖动态库
test -f install/lib/libHiggsIS.so
test -f install/lib/libHiggsOps.so

# 3. 配置并只构建必要目标
cmake -S . -B build/server -G Ninja \
  -DINTERFACING_BUILD_TESTS=OFF
cmake --build build/server -j 4

# 4. 检查 YAML
cat build/server/config/impl_a.yaml

# 5. 检查插件两个入口
nm -D --defined-only build/server/lib/libimpl_a.so \
  | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'

# 6. 检查运行时库
ldd build/server/lib/libimpl_a.so | grep 'not found' || true
ldd build/server/bin/main.out | grep 'not found' || true

# 7. 最后运行
build/server/bin/main.out build/server/config/impl_a.yaml
```

不要一开始只盯着“运行失败”。按这个顺序可以把问题划分为：依赖、配置、动态
符号、运行库和业务配置五个独立层次。

---

## 18. 最终记忆模型

```text
ConfigFactory：
从 YAML 决定加载谁，并把完整配置登记成 token

ClassLoader：
打开 .so、检查版本、找到工厂、调用工厂、验证对象类型

HCL_SO_VERSION：
让 .so 公开声明自己的 HCL 插件版本

NewInstance：
使用 token 取回配置并创建具体对象

Interface：
让主程序通过统一虚函数调用不同实现
```

一句话总结：

> YAML 负责选择，ConfigFactory 负责传递配置，ClassLoader 负责跨动态库创建对象，
> NewInstance 负责构造具体实现，Interface 负责屏蔽实现差异。
