# shannon_algo_db 静态/动态混合加载原理笔记

> 构建目录说明：本文部分实验记录沿用早期 `build/server` 路径。当前 Interfacing
> 高阶版正式使用 `build/debug` 和 `build/advanced`；实际命令以 `README.md` 和
> `docs/DEVELOPMENT_COMMAND_REFERENCE.md` 为准。

## 0. 本文定位

本文整理并深化 `shannon_algo_db` 标签 `v7.1.0-support-dynamic-loading` 的设计，目标是理解静态链接、运行时动态加载、工厂注册表、双 `NewInstance`、配置分流、嵌套加载、CMake 与测试，而不只是复制代码。

参考信息：

```text
仓库：https://github.higgsasset.com/Shannon/shannon_algo_db
标签：v7.1.0-support-dynamic-loading
提交：ead069a02eaa69e002486e0d40425d1eee1e8b53
本地参考：C:\LocalCode\shannon_algo_db_reference
```

---

## 1. 先区分三种容易混淆的机制

### 1.1 静态链接

静态库一般是 `libxxx.a`。链接器从 `.a` 中取出被引用的目标代码，放进最终可执行文件：

```text
main.o + libimpl_static.a
          ↓ 链接
       main.out
```

实现代码在程序启动前已经进入 `main.out`。运行时不再需要 `.a`，但新增或替换静态实现通常需要重新链接主程序。静态工厂不要求出现在 ELF 动态符号表，只要普通链接器能解析并把对应目标文件拉入程序即可。

### 1.2 启动时动态链接

构建时使用 `-lHiggsOps`，会让 `main.out` 记录 `NEEDED libHiggsOps.so`。Linux 启动程序时先加载这些 `.so`，然后进入 `main()`：

```bash
readelf -d main.out | grep NEEDED
ldd main.out
```

它使用共享库，但库名在链接阶段已经确定，不等于插件式动态加载。

### 1.3 运行时显式加载插件

程序启动后再读取配置，决定打开哪个 `.so`：

```text
main.out已经启动
  -> 读取YAML
  -> dlopen(某个.so)
  -> dlsym(工厂符号)
  -> 创建对象
```

HiggsIS `ClassLoader` 封装了 `dlopen/dlsym`、版本检查、异常转换和类型转换。业务代码调用 `higgsops::LoadClass<T>`，不应再自定义一套裸加载协议。

### 1.4 参考项目所说的“静态/动态”

```text
静态模式：实现代码通过.a进入进程，配置从进程内注册表选工厂
动态模式：配置给出.so路径，ClassLoader运行时打开并查工厂
```

两种方式都可以在运行时由配置选择。“静态”描述实现代码如何进入进程，不表示业务代码必须直接写死 `new ImplA()`。

---

## 2. shannon_algo_db 要解决什么问题

项目定义统一接口 `AlgoBusDbApi`，并提供 `AlgoDummyDbApi`、`AlgoVecDbApi` 等实现。它希望：常用实现可以静态进入程序；外部实现可以作为 `.so` 替换；调用方只依赖抽象接口；配置决定加载方式；组合对象的子实现也能独立选择加载方式。

因此引入：

```text
AlgoBusDbApiHolder
= 静态工厂注册表
+ 动态ClassLoader分发器
+ 嵌套对象加载协调器
```

关键文件：

| 文件 | 作用 |
| --- | --- |
| `AlgoBusDbApi.h/.cpp` | 公共接口、共同状态、库级 HCL 版本 |
| `AlgoBusDbApiHolder.h/.cpp` | 注册静态实现并选择加载路径 |
| `DummyDbApi.h/.cpp` | 简单叶子实现 |
| `VecDbApi.h/.cpp` | 可以加载子实现的组合实现 |
| `tests/test_holder.cpp` | 静态、动态及嵌套组合示例 |
| `AlgoBusDbApi/CMakeLists.txt` | 生成静态库或共享库 |
| `compile.sh` | 分别配置 static/so/debug 构建目录 |

---

## 3. 总体结构图

```text
                         配置Map
                            │
                            ▼
                  AlgoBusDbApiHolder
                            │
             ┌──────────────┴──────────────┐
             │                             │
        静态配置                       动态配置
             │                             │
             ▼                             ▼
    api_map_[className]          higgsops::LoadClass<T>
             │                             │
    NewInstance(Map)               ClassLoader<T>
             │                             │
             │                    NewInstance(token)
             │                             │
             │                    GetAssignedConfig
             │                             │
             └──────────────┬──────────────┘
                            ▼
                   NewInstance(Map)
                            │
                            ▼
                    new 具体实现(cfg)
                            │
                            ▼
                     AlgoBusDbApi对象
```

两条路径最终汇合到 `NewInstance(const config::Map&)`，因此业务构造和配置解释只有一份代码。

---

## 4. 静态工厂注册表

Holder 定义工厂函数指针：

```cpp
using NewInstanceImplType =
    HiggsIS::Loadable* (*)(
        const higgsops::config::Map& cfg);
```

从右向左读：`NewInstanceImplType` 是函数指针；函数接收 `const Map&`；返回 `Loadable*`。

注册表：

```cpp
std::map<std::string, NewInstanceImplType> api_map_;
```

逻辑内容：

```text
"shannon::algo::AlgoDummyDbApi"
  -> &AlgoDummyDbApi::NewInstance(Map)

"shannon::algo::AlgoVecDbApi"
  -> &AlgoVecDbApi::NewInstance(Map)
```

C++ 不能原生根据字符串执行 `new 某个类型`，所以该映射相当于一套轻量手工反射。

---

## 5. `AddDbApi<T>()` 如何注册实现

```cpp
template<typename T>
void AddDbApi() {
    static_assert(
        std::is_base_of<AlgoBusDbApi, T>::value,
        "T must be derived from AlgoBusDbApi");

    NewInstanceImplType f = &(T::NewInstance);
    _AddDbApi(typeid(T), f);
}
```

调用：

```cpp
holder->AddDbApi<AlgoDummyDbApi>();
holder->AddDbApi<AlgoVecDbApi>();
```

### 5.1 `static_assert`

它在编译期保证 `T` 继承 `AlgoBusDbApi`。错误类型不能进入注册表，问题不会推迟到运行时。

### 5.2 重载函数地址的选择

实现类有：

```cpp
NewInstance(const char*);
NewInstance(const config::Map&);
```

`&(T::NewInstance)` 单独看有重载歧义，但左侧 `NewInstanceImplType` 要求参数为 `const Map&`，编译器据此选择 Map 重载。这叫上下文重载解析。

### 5.3 `typeid` 和 demangle

`typeid(T).name()` 在 GCC 下常是修饰名。参考代码调用 `abi::__cxa_demangle`，得到完整名称：

```text
shannon::algo::AlgoDummyDbApi
```

然后执行：

```cpp
api_map_.emplace(className, factory);
```

因此配置里的静态类名要与完整命名空间一致。

---

## 6. 配置如何判断静态或动态

参考代码：

```cpp
bool is_dynamic = cfg["class"].IsMap();
std::string className;

if (is_dynamic) {
    auto cls = cfg["class"].AsMap();
    is_dynamic =
        cls.Contains("file") &&
        cls.Contains("ver");
    className = cls["class"].AsString();
} else {
    className = cfg["class"].AsString();
}
```

静态字符串格式：

```yaml
class: shannon::algo::AlgoDummyDbApi
```

静态 Map 格式：

```yaml
class:
  class: shannon::algo::AlgoDummyDbApi
```

动态格式：

```yaml
class:
  file: libShannonAlgoBusDbApi.so
  ver: 7.1.0
  class: shannon::algo::AlgoDummyDbApi
```

判断表：

| `class`形态 | `file/ver` | 模式 |
| --- | --- | --- |
| 字符串 | 不适用 | 静态 |
| Map | 不完整或没有 | 静态 |
| Map | 两者都有 | 动态 |

风险：想写动态配置却漏了 `ver`，参考实现会误走静态路径。新项目更适合显式增加：

```yaml
load_mode: static
```

或：

```yaml
load_mode: dynamic
```

再严格验证各模式的必需字段，避免配置错误悄悄改变执行路径。

---

## 7. 静态加载完整调用链

静态分支：

```cpp
auto it = api_map_.find(className);
if (it != api_map_.end()) {
    db_api = AlgoBusDbApiPtr(
        dynamic_cast<AlgoBusDbApi*>(
            it->second(cfg)));
}
```

逐步执行：

```text
读取静态class名称
  -> api_map_.find(className)
  -> 得到Map工厂函数地址
  -> factory(cfg)
  -> NewInstance(Map)
  -> new具体实现
  -> 返回Loadable*
  -> dynamic_cast<AlgoBusDbApi*>
  -> shared_ptr<AlgoBusDbApi>
```

这条路不需要 `dlopen`、`dlsym`、HCL版本、token 或 `GetAssignedConfig`，因为实现代码已经在进程中，调用方也已经持有 Map。

静态工厂不必进入 `.dynsym`，但必须被真正链接进程序。仅把一个 `.a` 放到链接列表中，不保证其中所有对象都会进入可执行文件；显式注册对工厂的引用会促使链接器拉入相关目标。

---

## 8. 动态加载完整调用链

动态分支：

```cpp
db_api = higgsops::LoadClass<AlgoBusDbApi>(cfg);
```

内部过程：

```text
提取class.file/class.ver/class.class
  -> ClassLoader<AlgoBusDbApi>
  -> 打开.so
  -> 查找HCL_DynamicLibVersion
  -> 比较配置版本与库版本
  -> 根据完整类名查NewInstance(char const*)
  -> LoaderHelper保存Map并生成token
  -> factory(token)
  -> GetAssignedConfig(token)
  -> NewInstance(Map)
  -> new具体实现
  -> dynamic_cast<AlgoBusDbApi*>
  -> 智能指针
```

动态工厂必须存在于动态符号表：

```bash
nm -D --defined-only libShannonAlgoBusDbApi.so |
c++filt | grep NewInstance
```

`-D` 很关键，因为 `dlsym` 查找的是动态符号。源码里存在函数不代表 `.dynsym` 中一定存在它。

版本入口也必须导出：

```bash
nm -D --defined-only libShannonAlgoBusDbApi.so |
grep HCL_DynamicLibVersion
```

HCL版本验证“配置声称的插件版本等于文件自身声明”，不能完全代替业务 Interface ABI 兼容校验。

---

## 9. 为什么同一实现有两个 `NewInstance`

```cpp
static HiggsIS::Loadable*
NewInstance(const char* token);

static HiggsIS::Loadable*
NewInstance(const higgsops::config::Map& cfg);
```

| 工厂 | 调用者 | 原因 |
| --- | --- | --- |
| `const char*` | Higgs ClassLoader | 动态插件 ABI 必须稳定 |
| `const Map&` | Holder静态注册表 | 同进程中可以直接传配置对象 |

`AlgoVecDbApi` 的推荐实现：

```cpp
HiggsIS::Loadable*
AlgoVecDbApi::NewInstance(const char* token)
{
    return AlgoVecDbApi::NewInstance(
        higgsops::GetAssignedConfig(token));
}

HiggsIS::Loadable*
AlgoVecDbApi::NewInstance(const config::Map& cfg)
{
    return new AlgoVecDbApi(cfg);
}
```

路径汇合：

```text
动态：token -> GetAssignedConfig -> NewInstance(Map) -> new
静态：                           NewInstance(Map) -> new
```

优点是配置默认值、字段校验、构造过程只有一份。`DummyDbApi` 当前不使用配置，所以两个重载都直接 `new`；新代码仍应采用 Vec 的委托方式，为以后增加配置留出一致行为。

---

## 10. token 的本质

动态工厂 ABI 固定为：

```cpp
HiggsIS::Loadable* (*)(const char*);
```

不同插件却需要完全不同的业务字段。框架不可能每增加一个业务字段就改变函数签名，所以采用：

```text
LoaderHelper保存完整Map
  -> 生成随机token
  -> token穿过稳定的const char* ABI
  -> 插件调用GetAssignedConfig(token)
  -> 取回完整Map
```

token 是临时配置注册表的索引，不是配置正文。实现应在 `NewInstance` 调用期间立刻取回配置，不应长期保存 token。

静态路径无需 token，因为调用方和实现位于同一 C++ 进程，可以直接传 `const Map&`。

---

## 11. `dynamic_cast` 的意义

统一工厂只保证返回：

```cpp
HiggsIS::Loadable*
```

Holder要求对象实现：

```cpp
AlgoBusDbApi
```

继承链：

```text
HiggsIS::Loadable
        ↑
   AlgoBusDbApi
        ↑
DummyDbApi/VecDbApi
```

`dynamic_cast<AlgoBusDbApi*>` 是运行时类型验证。如果工厂返回无关 `Loadable` 对象，转换返回空指针，而不是让业务把错误对象当成接口调用。

更稳健的实现应显式检查转换失败、释放原始对象并抛出包含类名的异常，不能只把空指针继续传下去。

---

## 12. Holder生命周期与 `shared_from_this`

Holder 继承：

```cpp
std::enable_shared_from_this<AlgoBusDbApiHolder>
```

对象创建成功后：

```cpp
db_api->SetHolder(shared_from_this());
```

因此 Holder 必须由 `shared_ptr` 管理。若在栈上构造再调用 `shared_from_this()`，可能抛出 `std::bad_weak_ptr`。

把 Holder 传给对象，是为了让 `VecDbApi` 加载子实现时继续调用同一个 Holder，复用相同注册表和模式规则。若双方都长期强持有对方，需要检查 `shared_ptr` 循环；一般服务反向引用可考虑 `weak_ptr`。

当前 `Interfacing` 只有平面 A/B，第一版不必为了模仿而加入递归 Holder 和 `shared_from_this`。

---

## 13. VecDbApi 为什么是高级示例的重点

`VecDbApi` 是组合对象，内部保存多个 `AlgoBusDbApiPtr`。它读取子配置并调用：

```cpp
holder_->GetAlgoBusDbApi(childConfig);
```

于是加载决策可以递归：

```text
外层配置 -> Holder -> 创建Vec
                    -> 把Holder交给Vec
Vec读取子配置 -> 同一个Holder -> 创建子对象
```

可以组合：

| 外层Vec | 内层实现 | 支持 |
| --- | --- | --- |
| 静态 | 静态 | 是 |
| 静态 | 动态 | 是 |
| 动态 | 静态 | 是 |
| 动态 | 动态 | 是 |

这说明参考设计不是只在 main 中写一次 `if`，而是把加载策略抽成了可以被任意组合对象复用的基础设施。

---

## 14. CMake怎样生成 `.a` 和 `.so`

参考代码：

```cmake
file(GLOB_RECURSE ALGOBUSDBAPI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(
    ShannonAlgoBusDbApi
    ${ALGOBUSDBAPI_SOURCES})
```

没有明确写 `STATIC/SHARED`，所以由 `BUILD_SHARED_LIBS` 决定：

```text
BUILD_SHARED_LIBS=OFF -> 静态库
BUILD_SHARED_LIBS=ON  -> 共享库
```

静态构建：

```bash
cmake . -Bbuild/static -G Ninja
cmake --build build/static --target install -j
```

动态构建：

```bash
cmake . -Bbuild/so -G Ninja \
  -DBUILD_SHARED_LIBS=ON
cmake --build build/so --target install -j
```

它是“同一源码、两个构建目录、分别编译”，不是单个 CMake 配置同时产生 `.a` 和 `.so`。

静态输出被重命名为 `libShannonAlgoBusDbApi_static.a`，与 `libShannonAlgoBusDbApi.so` 明确区分。

根 CMake 只在非 Debug 且 `BUILD_SHARED_LIBS=OFF` 时启用测试。测试程序因此链接静态实现，而动态用例还要求共享库已经存在并能被运行时找到。新工程应显式建立测试对共享插件的构建依赖，不能依赖旧构建残留。

---

## 15. HCL固定符号与重复定义风险

参考库把所有实现放在一份库中，只在公共 `AlgoBusDbApi.cpp` 生成一次：

```text
HCL_DynamicLibVersion
```

`Interfacing` 当前是每个实现独立 `.so`，所以每个动态插件需要一个版本入口。但若同一份 A/B 源码又编成静态库，并都把固定名称的 HCL 符号带入主程序，就可能产生重复定义。

解决方案一：

```cpp
#ifdef INTERFACING_DYNAMIC_PLUGIN
HCL_SO_VERSION(INTERFACE_VERSION)
#endif
```

只对动态目标定义宏。

更清晰的方案是拆分：

```text
impl_a.cpp                 实现和双工厂
impl_a_plugin_version.cpp  只放HCL_SO_VERSION
```

共享目标包含两个文件，静态目标只包含 `impl_a.cpp`。

---

## 16. 参考测试覆盖和不足

`test_holder.cpp` 覆盖：

1. 字符串形式静态 Dummy；
2. Map 形式静态 Dummy；
3. 动态 Dummy；
4. 静态 Vec + 静态子对象；
5. 动态 Vec + 静态子对象；
6. 动态 Vec + 动态子对象；
7. 静态 Vec + 动态子对象。

主要断言是：

```cpp
ASSERT_TRUE(db_api != nullptr);
```

模式主要靠日志观察：

```text
Static LoadClass: ...
Dynamic LoadClass: ...
```

这不能严格证明实际走了预期路径。若配置判断错误，而另一条路径恰好也能创建同名对象，非空断言仍可能通过。

更好的接口：

```cpp
enum class LoadMode {
    Static,
    Dynamic
};

struct LoadResult {
    std::unique_ptr<Interface> instance;
    LoadMode mode;
    std::string class_name;
};
```

测试直接断言：

```cpp
EXPECT_EQ(result.mode, LoadMode::Static);
EXPECT_EQ(result.mode, LoadMode::Dynamic);
```

模式属于“加载器怎样取得对象”，不属于实现对象本身。同一个 `ImplA` 可以静态或动态创建，不应让 `ImplA::GetLoadMode()` 返回固定模式。

---

## 17. 参考设计的优点

- 调用方始终使用一个 Holder 入口；
- 静态注册有编译期继承检查；
- 双工厂最终共享 Map 构造逻辑；
- 动态路径复用公司 ClassLoader 协议；
- 完整类名与工厂函数形成可配置映射；
- Holder 可以递归传给组合实现；
- 静态和动态可以在同一进程、同一配置体系中共存。

---

## 18. 不能直接照抄的地方

### 18.1 隐式模式判断

漏字段可能让动态配置误走静态路径，应考虑显式 `load_mode`。

### 18.2 测试只看非空和日志

自动测试应断言结构化 `LoadMode`，日志只能辅助诊断。

### 18.3 两套构建目录的隐含顺序

静态测试中的动态用例要求 `.so` 已存在。新工程应让一次构建明确产生两种产物并建立依赖。

### 18.4 C++ ABI风险

主程序与插件需要兼容的编译器、标准库 ABI、RTTI、异常设置、虚函数布局和 `_GLIBCXX_USE_CXX11_ABI`。版本字符串不能替代真实 ABI 管理。

### 18.5 动态库生命周期

插件对象存活期间不能卸载其 `.so`；对象虚函数和析构代码仍位于动态库中。

### 18.6 静态注册不是自动发现

实现即使已链接，未执行 `AddDbApi<T>()` 仍不会进入注册表。

---

## 19. 映射到 Interfacing

| shannon_algo_db | Interfacing |
| --- | --- |
| `AlgoBusDbApi` | `Interface` |
| `AlgoDummyDbApi` | `ImplA/ImplB` |
| `AlgoBusDbApiHolder` | 混合 `InterfaceLoader/Registry` |
| `AddDbApi<T>()` | 注册内置 A/B 静态工厂 |
| `LoadClass<AlgoBusDbApi>` | 当前 `LoadClass<Interface>` |
| `NewInstance(token)` | 当前动态工厂 |
| `NewInstance(Map)` | 待增加的静态工厂 |

建议结构：

```text
interface.h
interface_loader.h/.cpp
interface_registry.h/.cpp
builtin_interfaces.h/.cpp
impl_a/impl_a.h/.cpp
impl_a/impl_a_plugin_version.cpp
impl_b/impl_b.h/.cpp
impl_b/impl_b_plugin_version.cpp
```

`Registry` 保存类名到 Map 工厂；`HybridLoader` 按配置选择 Registry 或 `LoadClass`；`builtin_interfaces` 集中注册 A/B，使 main 仍不直接认识具体实现类。

---

## 20. 推荐配置格式

静态 A：

```yaml
load_mode: static
class:
  class: ImplA
message: static-A
```

动态 A：

```yaml
load_mode: dynamic
class:
  file: /absolute/path/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: dynamic-A
```

规则：静态必须有 `class.class` 且已注册；动态必须有 `file/ver/class`；非法模式或缺字段立即报告配置错误，不允许静默切换路径。

---

## 21. Interfacing双工厂模板

```cpp
class ImplA final : public Interface {
public:
    static HiggsIS::Loadable*
    NewInstance(const char* token);

    static HiggsIS::Loadable*
    NewInstance(const higgsops::config::Map& config);
};
```

```cpp
HiggsIS::Loadable*
ImplA::NewInstance(const char* token)
{
    return NewInstance(
        higgsops::GetAssignedConfig(token));
}

HiggsIS::Loadable*
ImplA::NewInstance(const config::Map& config)
{
    return new ImplA(
        config.GetOrDefault(
            "message", "default-A"));
}
```

```text
静态配置 ----------------------------┐
                                     ▼
                              NewInstance(Map)
                                     │
动态配置 -> token -> GetAssignedConfig┘
                                     │
                                     ▼
                                  new ImplA
```

---

## 22. Interfacing的CMake产物建议

一次构建明确生成：

```text
libimpl_a_static.a
libimpl_b_static.a
libimpl_a.so
libimpl_b.so
main.out
interfacing_tests
```

概念目标：

```cmake
add_library(impl_a_static STATIC impl_a.cpp)
add_library(impl_a SHARED
    impl_a.cpp impl_a_plugin_version.cpp)

add_library(impl_b_static STATIC impl_b.cpp)
add_library(impl_b SHARED
    impl_b.cpp impl_b_plugin_version.cpp)
```

主程序通过注册模块引用静态工厂；动态 YAML 指向共享插件。测试目标应显式依赖两类目标，保证全新 build 目录也能直接运行全部用例。

---

## 23. 推荐测试矩阵

| 用例 | 必须断言 |
| --- | --- |
| 静态 A | `mode == Static`，对象行为是A，配置到达A |
| 动态 A | `mode == Dynamic`，对象行为是A，配置到达A |
| 静态 B | `mode == Static`，对象行为是B |
| 动态 B | `mode == Dynamic`，对象行为是B |
| 未注册静态类 | 明确异常 |
| 动态文件不存在 | ClassLoader异常 |
| HCL版本错误 | 实例化前失败 |
| 动态缺file/ver | 配置错误，不退化为静态 |
| 非法load_mode | 配置错误 |
| Interface版本错误 | 统一版本校验失败 |

除了对象非空，还要断言模式和业务消息，分别证明“路径正确”和“配置传递正确”。

---

## 24. 二进制层验证

动态插件入口：

```bash
nm -D --defined-only build/server/lib/libimpl_a.so \
  | c++filt \
  | grep -E 'HCL_DynamicLibVersion|NewInstance'
```

静态工厂是否进入程序：

```bash
nm -C build/server/bin/main.out |
grep 'ImplA::NewInstance'
```

插件不应成为主程序固定 `NEEDED`：

```bash
readelf -d build/server/bin/main.out |
grep 'libimpl_a.so'
```

预期无输出，因为动态插件应由运行时配置打开。

运行库检查：

```bash
ldd build/server/bin/main.out | grep 'not found' || true
ldd build/server/lib/libimpl_a.so | grep 'not found' || true
```

---

## 25. 推荐实施顺序

```text
1. 只给ImplA增加Map工厂
2. 建立静态Registry，只注册ImplA
3. 静态配置成功创建A
4. 保留当前动态LoadClass路径
5. 同一个HybridLoader分别加载静态A和动态A
6. LoadResult明确返回实际模式并测试
7. 把协议复制到ImplB
8. 增加错误配置、版本、未注册类测试
9. 完成CMake双产物和复现手册
```

先只做 A 是为了控制变量。A 的最小闭环成功后，B 是同一协议的复制验证。

---

## 26. 最终心智模型

```text
静态链接：实现代码在构建main时进入程序
启动动态链接：系统按NEEDED在main前加载共享库
运行时动态加载：程序按配置dlopen插件

Registry：给已静态进入进程的实现建立“类名 -> 工厂”索引
Holder：根据配置选择Registry或ClassLoader
双NewInstance：char*服务动态ABI，Map服务静态调用
HCL_SO_VERSION：让动态库公开声明库级版本
LoadResult.mode：让程序和测试明确知道实际加载路径
```

一句话总结：

> shannon_algo_db 用 Holder 把“进程内静态工厂注册表”和“Higgs运行时动态类加载器”放到同一入口后面，再用双工厂让两条路径共享对象构造逻辑；配置负责选择路径，抽象 Interface 负责屏蔽具体实现差异。
---

## 27. 本次在服务器上的实际实施记录

### 27.1 实施位置与基线

本次不是只写设计稿，而是在下面的服务器工作树中完成了代码改造、编译和测试：

```text
服务器：10.214.2.51
用户：jinkaisheng
项目：/home/jinkaisheng/InterfacingJin/Interfacing
基线分支：main
基线提交：856ebf8fe906a4ac916ef6bd12b9f412f6a904b7
验证构建目录：build/advanced
```

我没有执行 `git commit`、`git push`、`git reset` 或删除旧构建目录。也就是说，代码已经在服务器工作树中实现并验证，但是否提交以及提交信息仍由你决定。

实施前先做了以下只读审计：

1. 检查分支、HEAD 和工作树，避免覆盖已有改动。
2. 检查 CMake、Ninja、GCC 版本。
3. 检查 `HiggsOps.Interface` 安装后的头文件和共享库位置。
4. 阅读 `ClassLoader.h`、`ConfigFactory.h` 和 `Loadable`，确认动态工厂签名、配置 token 的还原方式以及基类析构语义。
5. 检查 `HCL_SO_VERSION` 宏的真实定义，确认它会导出固定名称 `HCL_DynamicLibVersion`。
6. 阅读 `shannon_algo_db` 的 `v7.1.0-support-dynamic-loading` 分支，提取其 Registry/Holder/双工厂思路。

为了不把服务器上可能含内部依赖信息的源码整体下载到本机，我以公开 GitHub 仓库中与服务器相同的基线提交制作源码补丁，先执行 `git apply --check`，通过后才应用到服务器。这个动作的机制是：补丁只描述文本差异；`--check` 只验证上下文是否精确匹配，不修改文件；真正的 `git apply` 才落盘。

### 27.2 实际改动文件

| 文件 | 本次作用 |
| --- | --- |
| `interface_registry.h/.cpp` | 新增静态实现注册表，完成“类名 -> Map 工厂”的查找和对象所有权转换 |
| `builtin_interfaces.h/.cpp` | 新增组合根，只在这里显式注册 `ImplA`、`ImplB` |
| `interface_loader.h/.cpp` | 把静态 Registry 与动态 `higgsops::LoadClass` 放在统一入口下；返回实际加载模式 |
| `impl_a/impl_a.h`、`impl_b/impl_b.h` | 将类声明从 `.cpp` 提到头文件，让静态注册代码能在编译期看到工厂 |
| `impl_a/impl_a.cpp`、`impl_b/impl_b.cpp` | 为每个实现提供 `const char*` 和 `const Map&` 两个工厂重载 |
| `impl_a/plugin_version.cpp`、`impl_b/plugin_version.cpp` | 只给动态 `.so` 编译 HCL 版本入口 |
| `impl_a/CMakeLists.txt`、`impl_b/CMakeLists.txt` | 同一实现源分别生成 `.a` 静态库和 `.so` 动态插件 |
| 根 `CMakeLists.txt` | 连接静态实现、生成四份配置并组织统一 loader |
| `autotest/main.cpp` | 输出 loader 返回的真实模式，不靠猜测日志判断 |
| `tests/interface_tests.cpp` | 覆盖 A/B 静态、A/B 动态及多种失败路径 |
| `tests/CMakeLists.txt` | 确保测试依赖两类产物，并让旧版本插件继续走动态路径 |

---

## 28. 最终架构：一个入口，两条创建路径

```text
main.cpp
   |
   | LoadInterfaceWithModeFromConfig(yaml)
   v
HybridInterfaceLoader
   |
   +-- 解析 class / load_mode / file / ver
   |
   +-- static -----------------------------+
   |                                      |
   |   InterfaceRegistry                   |
   |   "ImplA" -> ImplA::NewInstance(Map) |
   |   "ImplB" -> ImplB::NewInstance(Map) |
   |                                      |
   +-- dynamic ----------------------------+
       higgsops::LoadClass<Interface>(Map)
          -> ClassLoader
          -> dlopen(file)
          -> 检查 HCL_DynamicLibVersion
          -> dlsym/工厂查找
          -> ImplX::NewInstance(token)
          -> GetAssignedConfig(token)
          -> ImplX::NewInstance(Map)

两条路径最后都得到 std::unique_ptr<Interface>
             |
             +-- 统一执行 Interface 版本兼容检查
             +-- 和 LoadMode 一起放进 LoadedInterface 返回
```

这里最重要的分层是：

- `main.cpp` 只认识配置、`Interface` 和 `LoadedInterface`。
- `HybridInterfaceLoader` 认识“怎样选择路径”，但不直接 `new ImplA`。
- `InterfaceRegistry` 认识静态工厂表，但不写死具体实现。
- `builtin_interfaces.cpp` 是唯一集中知道 `ImplA/ImplB` 的组合根。
- `ImplA/ImplB` 只负责从配置构造自身。
- Higgs ClassLoader 只负责跨共享库边界找到动态工厂并调用它。

“组合根”是依赖注入领域的概念：具体类型必须在某处与抽象接口接上线。把这个知识集中在一个文件，比让 `main.cpp`、loader 和测试到处包含 `impl_a.h` 更容易维护。

---

## 29. 静态注册表的代码和逐步解释

### 29.1 工厂类型

实际定义为：

```cpp
using StaticInterfaceFactory =
    HiggsIS::Loadable* (*)(const higgsops::config::Map& config);
```

从右向左读：`StaticInterfaceFactory` 是一个函数指针；函数接收 `const Map&`；返回 `Loadable*`。

为什么仍返回 `Loadable*`，而不是直接返回 `Interface*`？因为动态 HCL 工厂也以 `HiggsIS::Loadable` 为公共根。两条路径保持同一对象协议，可以在统一位置做运行时类型检查。

为什么静态工厂参数是 `const Map&`？静态调用完全发生在同一进程、同一 C++ ABI 内，配置已经解析成 `Map`，无需再转成 token。

### 29.2 模板注册

```cpp
template <typename T>
void Register(std::string class_name) {
    static_assert(std::is_base_of<Interface, T>::value,
                  "A statically registered type must derive from Interface");

    const StaticInterfaceFactory factory =
        static_cast<StaticInterfaceFactory>(&T::NewInstance);
    RegisterFactory(std::move(class_name), factory);
}
```

逐句理解：

1. `T` 在本项目里可能是 `ImplA` 或 `ImplB`。
2. `static_assert` 在编译期拒绝没有继承 `Interface` 的类型；错误越早发现越好。
3. 每个实现有两个同名 `NewInstance`，直接写 `&T::NewInstance` 会产生重载歧义。
4. `static_cast<StaticInterfaceFactory>` 明确要求编译器选择 `NewInstance(const Map&)`。
5. `std::move(class_name)` 把无需再使用的字符串资源交给注册表，避免一次不必要的复制。

### 29.3 注册发生在哪里

```cpp
void RegisterBuiltInInterfaces(InterfaceRegistry& registry) {
    registry.Register<ImplA>("ImplA");
    registry.Register<ImplB>("ImplB");
}
```

随后 loader 构造函数调用它：

```cpp
HybridInterfaceLoader::HybridInterfaceLoader() {
    RegisterBuiltInInterfaces(registry_);
}
```

因此“静态加载”不是扫描磁盘，也不是自动发现类。它的真正含义是：实现的机器码已经链接进当前进程，而且启动路径显式把其工厂地址放进了注册表。

### 29.4 创建和所有权安全

核心逻辑是：

```cpp
std::unique_ptr<HiggsIS::Loadable> raw(found->second(config));
if (!raw) {
    throw std::runtime_error("Static factory returned null: " + class_name);
}

Interface* typed = dynamic_cast<Interface*>(raw.get());
if (typed == nullptr) {
    throw std::runtime_error("factory returned wrong type");
}

raw.release();
return std::unique_ptr<Interface>(typed);
```

这里有一个容易忽略的异常安全细节：工厂返回裸指针后，代码立即交给 `unique_ptr<Loadable>`。如果 `dynamic_cast` 失败并抛异常，临时 `unique_ptr` 会销毁对象，不泄漏内存。类型正确时才 `release()`，再由 `unique_ptr<Interface>` 接管。

这要求 `Loadable`/`Interface` 有虚析构函数。本次实施前已经检查了依赖头文件，确认基类具备正确的多态析构语义。

---

## 30. 双 `NewInstance` 如何让两条路径复用构造逻辑

`ImplA` 的实际接口如下：

```cpp
static HiggsIS::Loadable* NewInstance(const char* config_token);
static HiggsIS::Loadable* NewInstance(
    const higgsops::config::Map& config);
```

实现如下：

```cpp
HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    return NewInstance(higgsops::GetAssignedConfig(config_token));
}

HiggsIS::Loadable* ImplA::NewInstance(
    const higgsops::config::Map& config) {
    return new ImplA(config.GetOrDefault("message", "default-A"));
}
```

调用关系不是两份平行实现，而是：

```text
静态：Registry -> NewInstance(Map) --------------------+
                                                       +-> new ImplA(...)
动态：ClassLoader -> NewInstance(token) -> 还原 Map ---+
```

因此默认值、校验和构造规则只写一遍。以后如果 `ImplA` 增加 `timeout` 字段，应在 `Map` 重载里处理，不能在两个工厂中各写一份。

`token` 不是 YAML 文本，也不是 `Map*` 的强制转换。它是 Higgs 配置系统分配的关联标识；`GetAssignedConfig(token)` 用它找回 ClassLoader 在调用工厂前绑定的那份配置。动态边界使用 `const char*` 的原因，是比把复杂 C++ 容器直接跨 `.so` 边界更稳定，也符合 HCL 预期的工厂协议。

---

## 31. `HybridInterfaceLoader::Load` 的完整决策规则

### 31.1 第一步：读类名和动态字段存在性

loader 接受两种 `class` 写法：

```yaml
# 简写，适合兼容参考项目的静态格式
class: ImplA
```

```yaml
# map 写法，本项目四份正式配置均采用它
class:
  class: ImplA
```

如果 `class` 是 map，loader 还记录 `file` 和 `ver` 是否存在。

### 31.2 第二步：确定模式

推荐显式写：

```yaml
load_mode: static
```

或：

```yaml
load_mode: dynamic
```

显式模式的优点是配置即文档，测试可以准确表达意图。为了兼容旧配置和 `shannon_algo_db` 参考格式，缺少 `load_mode` 时使用以下推断：

| `class.file` | `class.ver` | 推断结果 |
| --- | --- | --- |
| 无 | 无 | static |
| 有 | 有 | dynamic |
| 有 | 无 | 配置错误 |
| 无 | 有 | 配置错误 |

最后两项不能悄悄退回静态模式，否则用户把动态配置少写一个字段时，可能意外运行进程内旧实现。

### 31.3 第三步：分派

静态分支：

```cpp
instance = registry_.Create(class_name, config);
```

动态分支：

```cpp
instance = higgsops::LoadClass<Interface>(config);
```

静态模式若含 `file/ver` 会直接报错；动态模式若不是 class map 或缺 `file/ver` 也会报错。这叫“快速失败”：在配置边界给出清楚的错误，而不是让问题晚到 `dlopen` 或工厂中才暴露。

### 31.4 第四步：统一后处理

无论对象来自哪条路径，都会：

1. 检查空指针。
2. 默认调用 `ValidateInterfaceVersion(*instance)`。
3. 返回 `LoadedInterface{instance, mode, class_name}`。

```cpp
struct LoadedInterface {
    std::unique_ptr<Interface> instance;
    LoadMode mode;
    std::string class_name;
};
```

这比让测试分析日志可靠。相同的 `ImplA` 在两种模式中业务行为可以完全一致，唯一可信的路径证据应该来自完成分派的 loader 自身。

高阶版本只保留 `LoadInterfaceWithModeFromConfig()` 这一条文件加载入口。这样任何调用方
都必须接收 `LoadedInterface`，也就不会在不知情的情况下丢掉 `mode` 和 `class_name`。
若只需要业务对象，调用方应显式使用 `loaded.instance`；“忽略模式”由调用点明确表达，
而不是由另一个返回类型不同的兼容函数暗中完成。

---

## 32. 动态加载的底层调用链

当配置是 dynamic 时，实际逻辑可以分成八步：

```text
1. LoadConfigFile
   YAML -> Node -> Map

2. HybridInterfaceLoader
   验证 load_mode/file/ver/class

3. higgsops::LoadClass<Interface>(config)
   读取 class.file、class.ver、class.class

4. ClassLoader<Interface>
   打开指定 .so，并检查库级版本

5. HCL 查找工厂
   找到与 ImplA/ImplB 对应的 NewInstance 入口

6. factory(objectConfig.c_str())
   把 Higgs 分配的配置 token 传入插件

7. ImplA::NewInstance(token)
   GetAssignedConfig(token) -> NewInstance(Map)

8. ClassLoader
   dynamic_cast<Interface*>，交给 unique_ptr<Interface>
```

你之前询问的异常包装就发生在第 6 步附近：

```cpp
try { objPtr = factory(objectConfig.c_str()); }
catch (Exception& e1) { throw InstantiateException(typeid(T), e1); }
catch (std::exception& e2) { throw InstantiateException(typeid(T), e2.what()); }
catch (...) { throw InstantiateException(typeid(T), "Unknown exception"); }
```

含义是：插件工厂内部可能抛出 Higgs 异常、标准 C++ 异常或未知异常，ClassLoader 将它们统一包装成“实例化某个 T 失败”。`try` 不负责成功路径的类型转换；它只划定“调用外部工厂”这一风险边界，使上层获得一致的错误类型和上下文。

---

## 33. 为什么 HCL 版本代码必须从实现源中拆出来

依赖中的宏实际会生成固定符号：

```cpp
HCL_SO_VERSION(PROJECT_VERSION)
// 展开后导出弱符号 HCL_DynamicLibVersion()
```

动态插件各自拥有同名符号没有问题，因为符号分别位于不同 `.so`：

```text
libimpl_a.so -> HCL_DynamicLibVersion
libimpl_b.so -> HCL_DynamicLibVersion
```

但静态 A、静态 B 同时进入 `main.out` 时，如果两个 `.a` 都携带该固定入口，就会产生重复定义、错误选择或语义混乱。因此本次使用：

```text
impl_a.cpp             -> 同时编进 impl_a_static.a 和 libimpl_a.so
impl_a/plugin_version.cpp -> 只编进 libimpl_a.so

impl_b.cpp             -> 同时编进 impl_b_static.a 和 libimpl_b.so
impl_b/plugin_version.cpp -> 只编进 libimpl_b.so
```

这是“插件元数据属于共享库容器，不属于实现类本身”的设计。以后增加插件级 ABI 版本、构建 ID 等固定导出符号，也应优先放在插件专属翻译单元中。

---

## 34. CMake 双产物的实际实现

以 A 为例：

```cmake
add_library(impl_a_static STATIC impl_a.cpp)
target_link_libraries(impl_a_static PUBLIC ${INTERFACING_HIGGS_LIBRARIES})

add_library(impl_a SHARED impl_a.cpp plugin_version.cpp)
target_link_libraries(impl_a PRIVATE ${INTERFACING_HIGGS_LIBRARIES})
```

同一个 `impl_a.cpp` 会编译两次：一次进入静态归档 `.a`，一次成为共享对象 `.so` 的组成部分。这不是运行时把 `.a` 变成 `.so`；它是两个独立的链接目标。

loader 的链接关系是：

```cmake
target_link_libraries(interfacing_loader PUBLIC
    impl_a_static
    impl_b_static
    ${INTERFACING_HIGGS_LIBRARIES})
```

这里有两个 CMake 知识点：

1. `PUBLIC` 表示 `interfacing_loader` 自己需要这些库，而且最终链接它的 `main.out`/测试也要继承这些链接要求。
2. `.a` 本身只是目标文件归档。最终链接器发现 `builtin_interfaces.cpp` 引用了 `ImplA::NewInstance(Map)` 后，才从 `libimpl_a_static.a` 中抽取包含该符号的目标文件进入 `main.out`。

动态 `.so` 目标没有链接到 `interfacing_loader`。它们通过配置文件路径在运行时打开。因此 `main.out` 的 ELF `NEEDED` 中不应出现 `libimpl_a.so` 和 `libimpl_b.so`。

`add_subdirectory(impl_a/impl_b)` 必须先于定义依赖这些 target 的 loader；这是 CMake 的目标生成顺序要求，不是运行时加载顺序。

---

## 35. 四份正式配置及其含义

构建时生成：

### 35.1 静态 A

```yaml
load_mode: static
class:
  class: ImplA
message: configured-static-A
```

没有 `file`，因为实现不从磁盘插件加载。类名只用于 Registry 查表。

### 35.2 动态 A

```yaml
load_mode: dynamic
class:
  file: /绝对路径/build/advanced/lib/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-dynamic-A
```

- `file`：要 `dlopen` 的共享库。
- `ver`：HCL 检查的库级版本。
- `class`：要在插件中实例化的类。
- `message`：对象级业务配置，最终经 token 还原后到达 Map 工厂。

B 的配置结构相同，只把类、库文件和消息替换为 B。

原来的 `impl_a.yaml`、`impl_b.yaml` 继续保留，并明确作为 dynamic 别名，以免已有命令突然改变语义。

注意两套版本校验并不重复：

- `class.ver` 对应共享库/HCL 级别版本，通常在实例化前检查。
- `Interface::GetVersion()` 对应业务接口版本，在对象创建后由统一 loader 检查。

---

## 36. 本次测试矩阵和真实结果

服务器执行：

```bash
cd /home/jinkaisheng/InterfacingJin/Interfacing
ctest --test-dir build/advanced --output-on-failure -V
```

结果：

```text
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 0.13 sec
```

12 项分别证明：

1. 语义化接口版本比较规则。
2. 静态 A：`mode == Static`、类名 A、版本正确、A 收到静态消息。
3. 静态 B：`mode == Static`、类名 B、B 收到静态消息。
4. 动态 A：`mode == Dynamic`、类名 A、A 收到动态消息。
5. 动态 B：`mode == Dynamic`、类名 B、B 收到动态消息。
6. 旧接口实现被统一版本校验拒绝。
7. 主动关闭接口版本校验会让旧实现逃逸，证明校验的必要性。
8. YAML 声明的 HCL 库版本错误时被 ClassLoader 拒绝。
9. 静态类未注册时明确失败。
10. dynamic 缺 `file` 时明确失败。
11. 未知 `load_mode` 明确失败。
12. static 配置混入 dynamic 字段时明确失败。

测试没有仅使用 `ASSERT_NE(ptr, nullptr)`。它同时断言 `LoadMode` 和业务 message：前者证明走的是哪条路径，后者证明该路径把完整对象配置传到了实现。

---

## 37. 四种模式的真实运行输出

### 37.1 静态 A

```text
Loaded ImplA via static mode
Loaded interface version 1.0.0
Implementation A [configured-static-A] - print()
Implementation A - foo()
Implementation A - bar()
```

### 37.2 动态 A

```text
Loaded ImplA via dynamic mode
Loaded interface version 1.0.0
Implementation A [configured-dynamic-A] - print()
Implementation A - foo()
Implementation A - bar()
```

### 37.3 静态 B

```text
Loaded ImplB via static mode
Loaded interface version 1.0.0
Implementation B [configured-static-B] - print()
Implementation B - foo()
Implementation B - bar()
```

### 37.4 动态 B

```text
Loaded ImplB via dynamic mode
Loaded interface version 1.0.0
Implementation B [configured-dynamic-B] - print()
Implementation B - foo()
Implementation B - bar()
```

四次的 `print/foo/bar` 表现相似是“统一 Interface”的价值；模式行和不同 message 则证明它们并不是同一次加载伪装成两种模式。

---

## 38. ELF 层面的真实证据

构建目录实际包含：

```text
build/advanced/lib/libimpl_a_static.a
build/advanced/lib/libimpl_b_static.a
build/advanced/lib/libimpl_a.so
build/advanced/lib/libimpl_b.so
build/advanced/lib/libinterfacing_loader.a
build/advanced/bin/main.out
build/advanced/bin/interfacing_tests
```

### 38.1 主程序确实含静态实现符号

```bash
nm -C build/advanced/bin/main.out |
grep -E 'ImplA::NewInstance|ImplB::NewInstance'
```

实际能看到 A/B 的 `NewInstance(const char*)` 和 `NewInstance(const Map&)`。这证明对应实现对象文件已从静态归档进入最终可执行文件。

### 38.2 动态插件确实导出插件协议

```bash
nm -D --defined-only build/advanced/lib/libimpl_a.so
```

实际可见：

```text
HCL_DynamicLibVersion
ImplA::NewInstance(char const*)
ImplA::NewInstance(higgsops::config::Map const&)
```

这证明 `.so` 能提供 HCL 版本入口及对象工厂。

### 38.3 主程序没有把插件变成启动依赖

```bash
readelf -d build/advanced/bin/main.out
```

`NEEDED` 中有 `libHiggsOps.so`、`libHiggsIS.so` 和系统依赖，但没有 `libimpl_a.so`、`libimpl_b.so`。因此 A/B 插件不是操作系统在 main 启动前固定加载的，而是程序在 dynamic 分支按配置运行时打开。

### 38.4 没有缺失运行库

对 `main.out` 和 `libimpl_a.so` 执行 `ldd`，所有依赖都能解析，没有 `not found`。这证明“能成功链接”之外，服务器运行时搜索路径也满足执行要求。

---

## 39. 从零手工复现：完整操作手册

以下步骤假设代码处于上述基线版本，并且 `install/` 已经安装好项目要求的 Higgs 依赖。

### 39.1 第 0 步：进入项目并保护现场

```bash
ssh jinkaisheng@10.214.2.51
cd /home/jinkaisheng/InterfacingJin/Interfacing
git branch --show-current
git rev-parse HEAD
git status --short
```

原因：第一眼先区分“仓库原有改动”和“你即将制造的改动”。不要在工作树不明时运行 `reset --hard` 或覆盖文件。

### 39.2 第 1 步：确认依赖和工具链

```bash
cmake --version
ninja --version
g++ --version
find install/include -path '*HiggsIS*' -o -path '*higgsops*'
find install/lib -maxdepth 1 -type f -name 'libHiggs*.so*'
```

重点确认：

- 编译器支持 C++17。
- `ConfigFactory.h` 和 `ClassLoader.h` 可见。
- `libHiggsOps.so`、`libHiggsIS.so` 可见。
- 运行时能通过 RPATH 或 `LD_LIBRARY_PATH` 找到依赖。

### 39.3 第 2 步：先给实现增加 Map 工厂

把 `ImplA/ImplB` 类声明放入各自头文件，并声明两个重载：

```cpp
static HiggsIS::Loadable* NewInstance(const char* token);
static HiggsIS::Loadable* NewInstance(const higgsops::config::Map& config);
```

先让 token 重载委托 Map 重载，再编译。原因：这是静态、动态两条路径的共同落点；如果这一步不稳定，继续写 Registry 只会扩大排错范围。

### 39.4 第 3 步：拆出插件版本翻译单元

每个动态实现目录创建 `plugin_version.cpp`：

```cpp
#include <HiggsIS/ClassLoader.h>
HCL_SO_VERSION(PROJECT_VERSION)
```

从实现 `.cpp` 中移除该宏。原因：共享插件需要固定 HCL 入口，静态库不需要也不应携带它。

### 39.5 第 4 步：实现 Registry

创建 `interface_registry.h/.cpp`：

1. 定义 `Map -> Loadable*` 工厂指针。
2. `Register<T>` 做继承关系编译期校验。
3. 用显式函数指针类型消除重载歧义。
4. 注册时拒绝空名称、空工厂和重复类名。
5. 创建时立即用 `unique_ptr<Loadable>` 接管。
6. `dynamic_cast<Interface*>` 校验工厂产物。

原因：Registry 的职责是“索引已在进程内的实现”，而不是负责配置模式选择。

### 39.6 第 5 步：建立组合根

创建 `builtin_interfaces.h/.cpp`，只在 `.cpp` 中包含具体实现头文件：

```cpp
registry.Register<ImplA>("ImplA");
registry.Register<ImplB>("ImplB");
```

原因：具体依赖不可彻底消失，但可以集中到一个明确位置。main 和通用分派代码仍只面向抽象。

### 39.7 第 6 步：实现 Hybrid loader

按以下顺序实现：

```text
检查根 class
-> 提取 class_name/file/ver
-> 解析或推断 load_mode
-> 校验模式专属字段
-> Static: registry.Create
-> Dynamic: higgsops::LoadClass
-> 统一接口版本检查
-> 返回 instance + mode + class_name
```

先写显式模式，再增加缺 `load_mode` 的兼容推断。原因：显式规则更容易测试，兼容逻辑是附加层，不能反过来主导设计。

### 39.8 第 7 步：改 CMake 生成双产物

每个实现建立两个 target：

```cmake
add_library(impl_a_static STATIC impl_a.cpp)
add_library(impl_a SHARED impl_a.cpp plugin_version.cpp)
```

然后让 `interfacing_loader` 链接静态 target，不链接动态 target。测试 target 使用 `add_dependencies` 确保四种产物都在测试前生成。

### 39.9 第 8 步：生成四份配置

为 A/B 各生成 static/dynamic 配置。动态文件路径使用：

```cmake
$<TARGET_FILE:impl_a>
```

这是 CMake generator expression，生成阶段会展开成当前构建配置下真实插件路径，避免手写 `Debug/Release` 或平台相关文件名。

### 39.10 第 9 步：先配置到全新构建目录

```bash
cmake -S . -B build/advanced -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=ON
```

原因：不要依赖旧构建缓存、旧 `.so` 或旧生成配置。新目录能验证 CMake 依赖图本身完整。

### 39.11 第 10 步：编译

```bash
cmake --build build/advanced -j 4
```

预期至少生成：

```text
libimpl_a_static.a  libimpl_a.so
libimpl_b_static.a  libimpl_b.so
main.out            interfacing_tests
```

### 39.12 第 11 步：跑测试

```bash
ctest --test-dir build/advanced --output-on-failure -V
```

若失败，优先阅读第一个失败用例，不要同时修改多处。静态失败先看注册与链接；动态失败先看 `file/ver`、导出符号和运行库。

### 39.13 第 12 步：手动跑四份配置

```bash
export LD_LIBRARY_PATH="$PWD/install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

build/advanced/bin/main.out build/advanced/config/impl_a_static.yaml
build/advanced/bin/main.out build/advanced/config/impl_a_dynamic.yaml
build/advanced/bin/main.out build/advanced/config/impl_b_static.yaml
build/advanced/bin/main.out build/advanced/config/impl_b_dynamic.yaml
```

逐次确认第一行的 mode 和 message，不要只确认程序退出码为 0。

### 39.14 第 13 步：验证二进制事实

```bash
nm -C build/advanced/bin/main.out |
  grep -E 'ImplA::NewInstance|ImplB::NewInstance'

nm -D --defined-only build/advanced/lib/libimpl_a.so | c++filt |
  grep -E 'HCL_DynamicLibVersion|NewInstance'

readelf -d build/advanced/bin/main.out |
  grep -E 'libimpl_a.so|libimpl_b.so'

ldd build/advanced/bin/main.out | grep 'not found' || true
ldd build/advanced/lib/libimpl_a.so | grep 'not found' || true
```

第三条预期无输出；若出现 A/B `.so`，说明你把插件错误地作为普通共享依赖链接到了 main，而不是纯运行时加载。

### 39.15 第 14 步：审查并自行提交

```bash
git status --short
git diff --check
git diff --stat
git diff
```

确认无误后再由你决定提交：

```bash
git add CMakeLists.txt autotest impl_a impl_b tests \
  interface_loader.h interface_loader.cpp \
  interface_registry.h interface_registry.cpp \
  builtin_interfaces.h builtin_interfaces.cpp
git commit -m "support static and dynamic interface loading"
```

这里的 `git add/commit` 是手工复现建议，本次自动实施没有替你执行。

---

## 40. 如何增加第三个实现 ImplC

理解是否真正到位，可以用新增 C 做检验。需要：

1. 新建 `impl_c/impl_c.h/.cpp`，继承 `Interface`。
2. 实现 `NewInstance(token)` 和 `NewInstance(Map)`；前者委托后者。
3. 新建只用于动态 target 的 `impl_c/plugin_version.cpp`。
4. CMake 同时生成 `impl_c_static` 与 `impl_c`。
5. 在 `RegisterBuiltInInterfaces` 增加 `registry.Register<ImplC>("ImplC")`。
6. loader 本身不增加 `if (ImplC)`；否则说明抽象边界被破坏。
7. 增加 C static/C dynamic 两份配置和两项测试。
8. 用 `nm/readelf` 重做二进制验证。

如果只想让 C 支持动态插件，可以不把 `impl_c_static` 链接进 loader，也不注册 C；动态路径仍可按配置发现它。这正体现动态插件的部署可扩展性。

---

## 41. 常见故障的定位顺序

### 41.1 `Static implementation is not registered`

依次检查：

1. YAML 的 `class.class` 拼写。
2. `builtin_interfaces.cpp` 是否注册同名键。
3. `HybridInterfaceLoader` 构造时是否调用组合根。

这通常不是 `.a` 文件不存在，因为注册表查不到发生在真正调用工厂之前。

### 41.2 静态工厂出现 undefined reference

检查：

1. `impl_x_static` 是否包含 `impl_x.cpp`。
2. `interfacing_loader` 或最终 executable 是否链接 `impl_x_static`。
3. 工厂声明与定义的参数、namespace、const 是否完全一致。
4. 链接顺序或 `PUBLIC/PRIVATE` 传播是否正确。

### 41.3 动态加载找不到 `.so`

先查看生成后的 YAML 中 `class.file`，再执行：

```bash
ls -l /YAML/中的/绝对路径/libimpl_a.so
```

不要只看源码 CMake 中写了什么；动态加载器实际使用的是生成后配置中的字符串。

### 41.4 找不到 HCL 版本符号

```bash
nm -D --defined-only libimpl_a.so | c++filt |
grep HCL_DynamicLibVersion
```

如果无输出，检查 `plugin_version.cpp` 是否编进 SHARED target，以及符号是否因宏、可见性或链接时裁剪而消失。

### 41.5 `ClassLoader` 实例化异常

按异常层次读：

- `ClassNotFound`：库或类工厂发现阶段。
- `InstantiateException`：已经找到工厂，但工厂执行抛错。
- `dynamic_cast` 失败：工厂返回对象不满足请求的 `Interface` 类型。
- 接口版本 mismatch：对象已创建，但业务接口契约不兼容。

### 41.6 `ldd` 出现 `not found`

构建成功不代表部署机运行成功。检查：

```bash
readelf -d main.out | grep -E 'RPATH|RUNPATH'
echo "$LD_LIBRARY_PATH"
ldd main.out
```

开发期可设置 `LD_LIBRARY_PATH`；部署期更推荐合理的 install RPATH 或统一运行环境脚本，避免依赖当前 shell 的偶然状态。

---

## 42. 移植到另一台服务器时真正需要带什么

如果目标机只运行、不重新编译，通常需要：

```text
bin/main.out
lib/libHiggsOps.so 及其传递依赖
lib/libHiggsIS.so 及其传递依赖
动态模式需要的 libimpl_a.so / libimpl_b.so
config/*.yaml
```

静态 A/B 的机器码已经在 `main.out` 中，因此只使用 static 配置时不需要部署 A/B 插件 `.so`；但 HiggsOps/HiggsIS 当前仍是 main 的动态依赖，所以仍需部署它们。

如果目标机要重新构建，还需要源码、CMake 模块、Higgs 头文件/库和兼容编译器。C++ 插件尤其要注意：编译器、标准库、编译选项、RTTI、异常设置和公共头文件版本差异都可能造成 ABI 问题。最稳妥做法是让 main 和插件在同一工具链/基础镜像中构建。

迁移后必须重新运行：

```bash
ldd bin/main.out
ldd lib/libimpl_a.so
bin/main.out config/impl_a_static.yaml
bin/main.out config/impl_a_dynamic.yaml
```

动态 YAML 的绝对 `file` 路径也必须按新部署目录重新生成或改写。

---

## 43. 本次实现相对参考项目的保留与改进

保留了 `shannon_algo_db` 的核心：

- 静态 Registry。
- 动态 Higgs ClassLoader。
- 同一统一入口选择两条路径。
- 双 `NewInstance` 共享对象构造逻辑。

做了几项刻意改进：

1. 增加显式 `load_mode`，同时保留旧格式推断兼容。
2. `file/ver` 只出现一个时直接失败，防止动态配置意外退化为静态。
3. 返回 `LoadedInterface.mode`，测试不靠日志猜路径。
4. 静态 Registry 对重复名、空工厂、错误类型和空对象做防御性检查。
5. 把 HCL 固定版本符号隔离到动态插件专属源文件。
6. A/B 和 static/dynamic 都有对称测试，避免只证明一半设计。
7. 在全新 `build/advanced` 中一次生成全部产物，不依赖另一套构建目录的历史结果。

最终应这样理解这套机制：

> 配置决定“去进程内 Registry 找工厂”还是“让 ClassLoader 去磁盘插件找工厂”；双工厂把两种进入方式汇合到同一 Map 构造逻辑；Interface 统一对象使用方式；LoadedInterface 明确报告实际路径；CMake 则在构建期同时准备进程内机器码和可独立部署的插件机器码。

---

## 44. 先把“同时支持静态/动态加载”这句话说清楚

这是理解后续代码的前提。这里的“同时”描述的是**一个程序具备两种能力**，不是说一次对象创建会同时执行两遍。

### 44.1 错误理解：同一个对象同时静态创建和动态创建

下面这种理解是错误的：

```text
创建一个 ImplA
  ├── 同时从 main.out 中 new 一个
  └── 同时从 libimpl_a.so 中再 new 一个
```

一次加载请求最终只能选择一个模式：

```cpp
if (mode == LoadMode::Static) {
    // 只走静态路径
} else {
    // 只走动态路径
}
```

因此，对单个 `LoadedInterface` 而言：

```text
mode == Static
或
mode == Dynamic
```

两者互斥，不可能同时成立。

### 44.2 正确理解：同一个程序同时拥有两种加载能力

本项目所说的“同时支持”是：

```text
同一次 CMake 构建
   ├── 生成静态实现 .a
   └── 生成动态插件 .so

同一个 main.out
   ├── 内部已经链接静态实现
   └── 同时保留运行时 ClassLoader 能力

同一个 HybridInterfaceLoader 接口
   ├── 收到 static 配置时走 Registry
   └── 收到 dynamic 配置时走 ClassLoader
```

调用者不用换一套程序，也不用调用两个完全不同的业务接口。变化的只是配置：

```yaml
load_mode: static
```

或：

```yaml
load_mode: dynamic
```

### 44.3 “同时”可以分成三个层次

| 层次 | 本项目中的含义 | 是否已实现 |
| --- | --- | --- |
| 构建层 | 一次构建同时生成 `.a` 和 `.so` | 是 |
| 能力层 | 同一个 `main.out` 能按配置选择任一路径 | 是 |
| 对象层 | 同一进程可分别持有静态对象和动态对象 | API 结构支持 |

对象层可以写成：

```cpp
HybridInterfaceLoader loader;

LoadedInterface static_a =
    loader.LoadFromConfig("impl_a_static.yaml");

LoadedInterface dynamic_b =
    loader.LoadFromConfig("impl_b_dynamic.yaml");

// 两个对象可以同时活着，但每个对象各有自己的唯一来源。
static_a.instance->print();
dynamic_b.instance->print();
```

当前示例 `main.out` 每次只接收一份配置，所以单次命令只创建一个对象；这不限制 loader 在更大的业务进程中被调用多次。

### 44.4 用“固定员工和外部专家”记忆

可以把程序看成一家公司：

```text
静态实现
    = 入职时已经在公司的固定员工
    = 机器码在链接 main.out 时已经进入程序

动态实现
    = 运行中按需要请来的外部专家
    = 机器码位于单独 .so，按配置打开

HybridInterfaceLoader
    = 人员调度台

Interface
    = 所有人必须遵守的岗位职责
```

“同时支持”的含义是公司既能安排固定员工，也能请外部专家；不是让同一个人同时以两种身份入场。

---

## 45. 在读调用链之前，先认识每个文件的唯一职责

| 文件 | 只记住这一句话 |
| --- | --- |
| `autotest/main.cpp` | 接收配置路径并使用最终的 `Interface` 对象 |
| `interface_loader.h/.cpp` | 解析模式，并在静态/动态路径之间分流 |
| `interface_registry.h/.cpp` | 保存和调用已经链接进程序的静态工厂 |
| `builtin_interfaces.h/.cpp` | 把具体的 ImplA/ImplB 登记进静态注册表 |
| `impl_a/impl_a.h/.cpp` | A 的业务实现以及两个工厂入口 |
| `impl_a/plugin_version.cpp` | 只属于 A 动态插件的 HCL 库版本入口 |
| `impl_a/CMakeLists.txt` | 用同一份 A 源码生成静态库和动态插件 |
| 根 `CMakeLists.txt` | 把上述目标连接成完整程序并生成配置 |

从依赖方向记忆：

```text
main
  -> HybridInterfaceLoader
       -> InterfaceRegistry -> 静态工厂
       -> Higgs LoadClass    -> 动态工厂
  -> Interface

main 不直接 include ImplA/ImplB
```

---

## 46. 调用链的第 0 段：程序运行前，CMake 做了什么

运行时能选择两条路，是因为构建时已经准备好了两类产物。先看 A。

### 46.1 生成 A 的静态归档

```cmake
add_library(impl_a_static STATIC impl_a.cpp)
```

逐项解释：

- `add_library`：声明一个 CMake 库目标。
- `impl_a_static`：CMake 内部目标名。
- `STATIC`：要求链接成静态归档。
- `impl_a.cpp`：要编译的实现源文件。

结果是：

```text
impl_a.cpp
   -> 编译为 impl_a.cpp.o
   -> 归档进 libimpl_a_static.a
```

`.a` 不是运行时打开的插件。它是目标文件集合，最终链接 `main.out` 时，链接器从中抽取被引用的实现代码。

### 46.2 生成 A 的动态插件

```cmake
add_library(impl_a SHARED
    impl_a.cpp
    plugin_version.cpp)
```

逐项解释：

- `impl_a`：动态 target 名。
- `SHARED`：生成共享库。
- `impl_a.cpp`：同一份 A 业务实现也编进 `.so`。
- `plugin_version.cpp`：给 `.so` 增加 HCL 要求的固定版本符号。

结果是：

```text
impl_a.cpp + plugin_version.cpp
   -> libimpl_a.so
```

同一份 `impl_a.cpp` 被编译两次并不矛盾：

```text
第一次编译结果属于 libimpl_a_static.a
第二次编译结果属于 libimpl_a.so
```

这是两份独立机器码载体。

### 46.3 为什么 `plugin_version.cpp` 只进入动态库

```cmake
add_library(impl_a_static STATIC impl_a.cpp)
add_library(impl_a SHARED impl_a.cpp plugin_version.cpp)
```

静态 target 没有 `plugin_version.cpp`，动态 target 才有。原因是 `HCL_SO_VERSION` 导出固定名称：

```text
HCL_DynamicLibVersion
```

A.so 和 B.so 分处两个共享库，各自拥有这个名称没有问题。如果静态 A/B 都把它带入同一个 `main.out`，就会产生同名符号冲突或含义不清。

### 46.4 把静态实现接入 loader

根 CMake：

```cmake
add_library(interfacing_loader STATIC
    interface_loader.cpp
    interface_registry.cpp
    builtin_interfaces.cpp)
```

含义是将：

```text
模式分流 + 静态注册表 + 内置实现登记
```

组合成 `libinterfacing_loader.a`。

随后：

```cmake
target_link_libraries(interfacing_loader PUBLIC
    impl_a_static
    impl_b_static
    ${INTERFACING_HIGGS_LIBRARIES})
```

逐行理解：

- `interfacing_loader` 需要 A/B 的静态实现。
- 因为 `builtin_interfaces.cpp` 会取 `ImplA::NewInstance(Map)` 和 `ImplB::NewInstance(Map)` 的地址。
- `PUBLIC` 让最终使用 loader 的 `main.out` 继承这些链接依赖。
- `${INTERFACING_HIGGS_LIBRARIES}` 提供配置、ClassLoader、日志及其传递依赖。

注意这里**没有**把 `impl_a`、`impl_b` 两个 SHARED target 链接给 loader：

```text
impl_a_static / impl_b_static -> 链接进 main.out
impl_a.so / impl_b.so         -> 运行时按配置打开
```

这一步完成后，`main.out` 才真正同时具备：

```text
内置工厂机器码 + 动态 ClassLoader 机器码
```

---

## 47. 调用链的第 1 段：从 `main()` 开始逐行走

服务器当前 `autotest/main.cpp` 的核心代码：

```cpp
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <config.yaml>" << std::endl;
        return 1;
    }
```

逐行解释：

- `argc` 是命令行参数数量。
- `argv` 是参数字符串数组。
- 程序要求恰好有两个参数：程序名和 YAML 路径。
- 如果没有配置文件，加载器就不知道选择哪个类、哪种模式，因此直接返回错误码 1。

例如：

```bash
build/advanced/bin/main.out \
  build/advanced/config/impl_a_static.yaml
```

这时：

```text
argc    = 2
argv[0] = build/advanced/bin/main.out
argv[1] = build/advanced/config/impl_a_static.yaml
```

下一段：

```cpp
LoadedInterface loaded =
    LoadInterfaceWithModeFromConfig(argv[1]);
```

这一行是整个业务入口：

- `argv[1]` 把配置路径传给 loader。
- 函数内部会解析 YAML、选择模式、创建对象、检查版本。
- 返回值不是单纯的指针，而是 `LoadedInterface`。

`LoadedInterface` 包含：

```cpp
std::unique_ptr<Interface> instance;
LoadMode mode;
std::string class_name;
```

三者分别回答：

```text
instance   -> 创建出了哪个可调用对象
mode       -> 它通过哪条路径创建
class_name -> 配置要求的实现类名
```

下一段：

```cpp
std::cout << "Loaded " << loaded.class_name << " via "
          << ToString(loaded.mode) << " mode" << std::endl;
```

它不根据业务输出猜模式，而是直接打印分流器保存的事实：

```text
Loaded ImplA via static mode
```

或：

```text
Loaded ImplA via dynamic mode
```

接下来：

```cpp
std::cout << "Loaded interface version "
          << loaded.instance->GetVersion() << std::endl;
loaded.instance->print();
loaded.instance->foo();
loaded.instance->bar();
```

这里全部通过 `Interface*` 的虚函数调用：

- main 不知道对象来自 `.a` 还是 `.so`。
- main 不知道它是 `ImplA` 还是 `ImplB` 的 C++ 具体类型。
- 虚函数表会把调用分派到真实对象的实现。

异常处理：

```cpp
catch (const HiggsIS::Exception& error) { ... }
catch (const std::exception& error) { ... }
```

- 第一项接住 ClassLoader/Higgs 体系异常。
- 第二项接住配置校验、版本检查和 Registry 抛出的标准异常。
- 创建失败时返回 2，避免把失败伪装成正常退出。

---

## 48. 调用链的第 2 段：便利函数创建统一 loader

`main()` 调用：

```cpp
LoadInterfaceWithModeFromConfig(argv[1]);
```

该函数实现是：

```cpp
LoadedInterface LoadInterfaceWithModeFromConfig(
    const std::string& config_file,
    bool validate_version)
{
    HybridInterfaceLoader loader;
    return loader.LoadFromConfig(
        config_file, validate_version);
}
```

逐行解释：

```cpp
HybridInterfaceLoader loader;
```

在栈上创建一个统一加载器。构造 loader 时会顺带建立静态注册表，后面详细展开。

```cpp
return loader.LoadFromConfig(
    config_file, validate_version);
```

将文件路径交给成员函数。`validate_version` 默认是 `true`，因此正常调用默认开启接口版本检查。

这里 loader 是局部对象，但返回的 `unique_ptr<Interface>` 拥有真实实例；返回后对象不会因为 loader 离开作用域而自动丢失。

高阶实现不再提供“只返回 `std::unique_ptr<Interface>`”的第二个文件加载接口。调用方统一
写成：

```cpp
LoadedInterface loaded =
    LoadInterfaceWithModeFromConfig(config_file);

loaded.instance->print();
std::cout << ToString(loaded.mode) << std::endl;
```

这样静态和动态两条路径都穿过同一个入口，测试也能直接断言 `loaded.mode`。如果调用方
确实只关心对象，它仍可使用 `loaded.instance`，但不能再绕过加载模式这一项返回信息。

---

## 49. 调用链的第 3 段：loader 构造时注册静态实现

`HybridInterfaceLoader` 内部有成员：

```cpp
InterfaceRegistry registry_;
```

构造函数：

```cpp
HybridInterfaceLoader::HybridInterfaceLoader()
{
    RegisterBuiltInInterfaces(registry_);
}
```

执行顺序：

```text
先构造空 registry_
    -> 再进入 HybridInterfaceLoader 构造函数体
    -> 把 registry_ 交给 RegisterBuiltInInterfaces
```

组合根代码：

```cpp
void RegisterBuiltInInterfaces(
    InterfaceRegistry& registry)
{
    registry.Register<ImplA>("ImplA");
    registry.Register<ImplB>("ImplB");
}
```

第一行注册后，逻辑表变成：

```text
"ImplA" -> ImplA::NewInstance(const Map&)
```

第二行注册后：

```text
"ImplA" -> ImplA::NewInstance(const Map&)
"ImplB" -> ImplB::NewInstance(const Map&)
```

`Register<T>` 内部：

```cpp
static_assert(
    std::is_base_of<Interface, T>::value,
    "A statically registered type must derive from Interface");
```

含义是编译期间检查 `ImplA/ImplB` 必须继承 `Interface`。如果把完全无关的类型登记进来，程序直接无法通过编译。

下一行：

```cpp
const StaticInterfaceFactory factory =
    static_cast<StaticInterfaceFactory>(
        &T::NewInstance);
```

逐部分理解：

- `&T::NewInstance`：取得静态成员函数地址，不调用函数。
- `T` 为 `ImplA` 时，就是取得 `ImplA::NewInstance` 地址。
- 因为 ImplA 有 token 和 Map 两个重载，单写地址会有歧义。
- `static_cast<StaticInterfaceFactory>` 指定选择 `const Map&` 重载。

最后：

```cpp
RegisterFactory(std::move(class_name), factory);
```

进入非模板函数，将类名和工厂地址插入：

```cpp
factories_.emplace(
    std::move(class_name), factory);
```

`factories_` 就是本项目对应 `shannon_algo_db::api_map_` 的结构。

此时只完成了“登记”，没有创建 A/B 对象。

---

## 50. 调用链的第 4 段：把 YAML 文件解析成 Map

接下来执行：

```cpp
LoadedInterface HybridInterfaceLoader::LoadFromConfig(
    const std::string& config_file,
    bool validate_version) const
{
    const higgsops::config::Node root =
        higgsops::config::LoadConfigFile(config_file);
    return Load(root.AsMap(), validate_version);
}
```

第一行调用：

```cpp
higgsops::config::LoadConfigFile(config_file)
```

把磁盘中的 YAML 解析成通用 `Node`。

为什么先是 `Node`？因为配置根节点理论上可能是：

```text
Map、Array 或单值
```

本项目要求根节点必须是键值 Map，所以：

```cpp
root.AsMap()
```

把它转换为：

```cpp
higgsops::config::Map
```

然后进入真正的模式分流函数：

```cpp
Load(configMap, validate_version)
```

到这一步还没有决定静态或动态。

---

## 51. 调用链的第 5 段：逐行解析 class 信息

进入：

```cpp
HybridInterfaceLoader::Load(const Map& config, ...)
```

### 51.1 必须存在 `class`

```cpp
if (!config.Contains("class")) {
    throw std::invalid_argument(
        "Root configuration is missing required field: class");
}
```

没有 `class` 就无法知道创建哪个实现，所以立即失败。

### 51.2 取出 class 节点

```cpp
const Node class_node = config["class"];
const bool class_is_map = class_node.IsMap();
```

`class_node` 可能是字符串：

```yaml
class: ImplA
```

也可能是 Map：

```yaml
class:
  class: ImplA
```

或：

```yaml
class:
  file: /path/libimpl_a.so
  ver: 1.0.0
  class: ImplA
```

`class_is_map` 只描述数据形状，**不直接等于动态模式**。

### 51.3 准备三个解析结果

```cpp
std::string class_name;
bool has_file = false;
bool has_version = false;
```

它们初始含义：

```text
class_name  尚未读取
has_file    默认没有插件路径
has_version 默认没有插件库版本
```

### 51.4 class 是 Map 时

```cpp
if (class_is_map) {
    const Map class_info = class_node.AsMap();
```

将 class 节点转换成 Map，之后才能查里面的 `class/file/ver`。

```cpp
class_name = RequiredString(
    class_info, "class", "class configuration");
```

要求 `class.class` 必须存在并且能转换为字符串。例如取得：

```text
ImplA
```

再执行：

```cpp
has_file = class_info.Contains("file");
has_version = class_info.Contains("ver");
```

这里只记录字段是否存在，还没有打开 `.so`。

### 51.5 class 是字符串时

```cpp
else if (class_node.IsValue()) {
    class_name = class_node.AsString();
}
```

用于兼容参考项目的静态简写：

```yaml
class: ImplA
```

因为字符串里没有 `file/ver`，前面的默认值继续保持 `false`。

### 51.6 其他形状直接拒绝

```cpp
else {
    throw std::invalid_argument(
        "Root class must be a string or a map");
}
```

例如 `class` 是数组时，loader 不猜测含义。

---

## 52. 调用链的第 6 段：决定这一次请求走哪条路

### 52.1 先准备模式变量

```cpp
LoadMode mode;
const bool explicit_mode =
    config.Contains("load_mode");
```

- `mode` 将保存最终决定。
- `explicit_mode` 表示用户有没有明确写模式。

### 52.2 显式模式

```cpp
if (explicit_mode) {
    const std::string mode_text = RequiredString(
        config, "load_mode", "Root configuration");
```

读取根配置中的：

```yaml
load_mode: static
```

或：

```yaml
load_mode: dynamic
```

接下来：

```cpp
if (mode_text == "static") {
    mode = LoadMode::Static;
} else if (mode_text == "dynamic") {
    mode = LoadMode::Dynamic;
} else {
    throw std::invalid_argument(...);
}
```

字符串只在配置边界使用；进入程序后转换成强类型枚举，防止后续到处比较容易拼错的字符串。

### 52.3 缺少显式模式时的兼容推断

```cpp
else {
    if (has_file != has_version) {
        throw std::invalid_argument(...);
    }
```

`!=` 在两个 bool 上相当于异或：

| has_file | has_version | `!=` | 含义 |
| --- | --- | --- | --- |
| false | false | false | 两者都没有，可以静态 |
| true | true | false | 两者都有，可以动态 |
| true | false | true | 配置残缺，报错 |
| false | true | true | 配置残缺，报错 |

然后：

```cpp
mode = (has_file && has_version)
           ? LoadMode::Dynamic
           : LoadMode::Static;
```

这是三元表达式：

```text
file 和 ver 都有    -> Dynamic
file 和 ver 都没有  -> Static
```

这是为了兼容 `shannon_algo_db` 旧格式。新配置仍推荐显式写 `load_mode`。

此处是整条调用链真正的**岔路口**。

---

## 53. 调用链的第 7A 段：静态路径逐行执行

先创建统一指针变量：

```cpp
std::unique_ptr<Interface> instance;
```

此时 `instance == nullptr`，稍后由静态或动态分支中的一个赋值。

进入静态分支：

```cpp
if (mode == LoadMode::Static) {
```

### 53.1 拒绝静态配置夹带动态字段

```cpp
if (has_file || has_version) {
    throw std::invalid_argument(
        "Static configuration must not contain class.file or class.ver");
}
```

如果用户明确写了 `load_mode: static`，却又提供 `.so` 路径，配置意图相互矛盾。程序选择报错，而不是偷偷忽略字段。

### 53.2 调用静态 Registry

```cpp
instance = registry_.Create(
    class_name, config);
```

假设 `class_name == "ImplA"`，调用跳转到：

```cpp
InterfaceRegistry::Create("ImplA", config)
```

Registry 第一行：

```cpp
const auto found =
    factories_.find(class_name);
```

在表中查找：

```text
键："ImplA"
值：ImplA::NewInstance(Map) 的函数地址
```

若没有找到：

```cpp
if (found == factories_.end()) {
    throw std::runtime_error(
        "Static implementation is not registered: " +
        class_name);
}
```

这说明“实现源码存在”不等于“可以静态加载”。它还必须：

```text
编译进 .a
-> 链接进 main.out
-> Register<ImplA>("ImplA")
```

### 53.3 通过函数指针调用 Map 工厂

```cpp
std::unique_ptr<HiggsIS::Loadable> raw(
    found->second(config));
```

拆开看相当于：

```cpp
StaticInterfaceFactory factory = found->second;
HiggsIS::Loadable* pointer = factory(config);
std::unique_ptr<HiggsIS::Loadable> raw(pointer);
```

对于 A，真正调用：

```cpp
ImplA::NewInstance(
    const higgsops::config::Map& config)
```

注意静态路径：

```text
不打开 libimpl_a.so
不生成 token
不调用 GetAssignedConfig
```

### 53.4 A 的 Map 工厂创建对象

```cpp
HiggsIS::Loadable* ImplA::NewInstance(
    const higgsops::config::Map& config)
{
    return new ImplA(
        config.GetOrDefault(
            "message", "default-A"));
}
```

逐步执行：

1. `GetOrDefault("message", "default-A")` 查业务配置。
2. 配置有 `message` 就使用配置值。
3. 没有就使用 `default-A`。
4. `new ImplA(...)` 在堆上创建真实 A 对象。
5. 返回类型向上转换成 `HiggsIS::Loadable*`。

构造函数：

```cpp
ImplA::ImplA(std::string message)
    : message_(std::move(message)) {}
```

把传入字符串移动进成员 `message_`，以后 `print()` 使用它。

### 53.5 Registry 立即接管裸指针

```cpp
std::unique_ptr<HiggsIS::Loadable> raw(...);
```

工厂协议返回裸指针，但 Registry 立即放进 `unique_ptr`。如果后续检查失败，`raw` 会自动删除对象，避免泄漏。

### 53.6 检查真实业务类型

```cpp
Interface* typed =
    dynamic_cast<Interface*>(raw.get());
```

问题是：工厂承诺返回 `Loadable*`，但 loader 最终需要 `Interface*`。`dynamic_cast` 在运行时检查真实对象是否继承 `Interface`。

失败时：

```cpp
if (typed == nullptr) {
    throw std::runtime_error(...);
}
```

抛异常时 `raw` 仍拥有对象，会自动释放。

成功时：

```cpp
raw.release();
return std::unique_ptr<Interface>(typed);
```

- `release()` 只放弃 `raw` 的所有权，不删除对象。
- 新的 `unique_ptr<Interface>` 接管同一个对象。
- 最终返回给 Hybrid loader。

至此静态分支完成：

```text
Registry
 -> Map 工厂
 -> new ImplA
 -> 类型检查
 -> unique_ptr<Interface>
```

---

## 54. 调用链的第 7B 段：动态路径逐行执行

如果 `mode == Dynamic`，进入 `else`。

### 54.1 动态配置必须使用 class Map

```cpp
if (!class_is_map) {
    throw std::invalid_argument(
        "Dynamic configuration requires a class map");
}
```

原因是动态加载至少要同时表达：

```text
file  -> 哪个共享库
ver   -> 期望库版本
class -> 库中的哪个类
```

一个普通字符串放不下这三项结构化信息。

### 54.2 再次执行必需字段检查

```cpp
const Map class_info = class_node.AsMap();
(void)RequiredString(
    class_info, "file", "Dynamic class configuration");
(void)RequiredString(
    class_info, "ver", "Dynamic class configuration");
```

这里不使用返回字符串，所以显式转成 `(void)`。目的只是强制执行：

```text
字段存在检查 + 字符串类型转换检查
```

### 54.3 进入 Higgs 动态加载入口

```cpp
instance =
    higgsops::LoadClass<Interface>(config);
```

模板参数 `<Interface>` 表示：

> 我要从插件中创建一个最终可当作 `Interface` 使用的对象。

它不是要求具体类名叫 `Interface`。具体类名仍来自 YAML 的 `class.class`。

### 54.4 `LoadClass` 读取插件三要素

Higgs `ConfigFactory.h` 中的核心逻辑：

```cpp
const config::Map& classInfo =
    configNode["class"].AsMap();
```

从完整业务配置中取得 `class` 子 Map。

```cpp
HiggsIS::ClassLoader<Interface> loader(
    classInfo["file"].AsString(),
    classInfo["ver"].AsString(),
    classInfo["class"].AsString());
```

构造 ClassLoader 时传入：

```text
file  = /.../libimpl_a.so
ver   = 1.0.0
class = ImplA
```

`ClassLoaderBase` 在其内部负责概念上的：

```text
打开共享库
-> 检查 HCL_DynamicLibVersion
-> 根据类名解析 NewInstance 工厂地址
```

这一阶段加载的是 `.so`，还没有真正拿到业务对象。

### 54.5 `LoaderHelper` 暂存完整配置

```cpp
LoaderHelper helper(configNode);
```

概念上执行：

```text
生成随机 token
-> 临时表[token] = 完整 configNode
```

例如：

```text
token = "a81f..."

临时配置表["a81f..."] = {
    class: {...},
    message: configured-dynamic-A
}
```

### 54.6 token 穿过固定工厂边界

```cpp
return loader.NewInstance(
    helper.token.data());
```

`helper.token.data()` 取得 `const char*`。随后进入 `ClassLoader<T>::NewInstance`：

```cpp
Loadable* objPtr;
```

先声明接收工厂结果的基类裸指针。

```cpp
try {
    objPtr = factory(objectConfig.c_str());
}
```

- `factory` 是从 `.so` 中找到的函数地址。
- `objectConfig` 此时实际装的是 token 字符串。
- `c_str()` 将 `std::string` 暴露为 HCL 工厂规定的 `const char*`。
- 这一行真正跨入插件代码。

三个 catch：

```cpp
catch (Exception& e1) { ... }
catch (std::exception& e2) { ... }
catch (...) { ... }
```

分别捕获 Higgs 异常、标准异常和未知异常，并统一包装为“实例化 T 失败”。

### 54.7 进入 A 的 token 工厂

插件中的真实函数：

```cpp
HiggsIS::Loadable* ImplA::NewInstance(
    const char* config_token)
{
    return NewInstance(
        higgsops::GetAssignedConfig(
            config_token));
}
```

按求值顺序理解：

1. `config_token` 是领取配置的临时索引。
2. `GetAssignedConfig(config_token)` 从临时表找回完整 Map。
3. 取得的 Map 作为参数调用另一个同名重载。
4. 重载解析选择 `NewInstance(const Map&)`。

于是动态路径汇入与静态路径相同的代码：

```cpp
return new ImplA(
    config.GetOrDefault(
        "message", "default-A"));
```

这就是两个 `NewInstance` 的意义：

```text
token 重载 = 动态协议适配器
Map 重载   = 唯一真实构造逻辑
```

### 54.8 ClassLoader 检查空指针和类型

工厂返回后：

```cpp
if (objPtr == nullptr)
    throw InstantiateException(...);
```

空对象被视为实例化失败。

接着：

```cpp
Interface* retPtr =
    dynamic_cast<Interface*>(objPtr);
```

验证插件返回的真实对象确实实现 `Interface`。

失败时删除错误对象并抛出类型异常；成功时：

```cpp
return std::unique_ptr<Interface>(retPtr);
```

把对象所有权返回给 `higgsops::LoadClass`，再返回给 `HybridInterfaceLoader`。

### 54.9 `LoaderHelper` 清理 token

`LoadClass` 返回前后，局部 `helper` 离开作用域，其析构函数从临时配置表删除 token。

因此 token 必须在 `NewInstance(token)` 调用期间立即兑换，不能保存起来以后再用。

至此动态分支完成：

```text
LoadClass
 -> 打开 .so
 -> 查工厂
 -> token 暂存配置
 -> 插件 token 工厂
 -> 取回 Map
 -> Map 工厂
 -> new ImplA
 -> 类型检查
 -> unique_ptr<Interface>
```

---

## 55. 调用链的第 8 段：两条路径重新汇合

在分支结束后，变量类型完全相同：

```cpp
std::unique_ptr<Interface> instance;
```

区别只在来源：

```text
静态：instance 来自 registry_.Create
动态：instance 来自 higgsops::LoadClass
```

接下来是共同逻辑。

### 55.1 防御空指针

```cpp
if (!instance) {
    throw std::runtime_error(
        "Loader returned null for class: " +
        class_name);
}
```

无论哪条路径，最终都不允许把空对象交给 main。

### 55.2 统一接口版本检查

```cpp
if (validate_version) {
    ValidateInterfaceVersion(*instance);
}
```

`*instance` 把 `unique_ptr` 解引用成 `Interface&`。

内部：

```cpp
const std::string implementation_version =
    instance.GetVersion();
```

通过虚函数询问具体实现声明的接口版本。

然后：

```cpp
if (!IsInterfaceVersionCompatible(
        INTERFACE_VERSION,
        implementation_version)) {
    throw std::runtime_error(...);
}
```

这项校验与动态配置中的 `class.ver` 不同：

```text
class.ver
    = HCL 共享库级版本检查
    = 只在动态路径出现

Interface::GetVersion()
    = 业务接口契约检查
    = 静态和动态都执行
```

### 55.3 返回对象、模式和类名

```cpp
return LoadedInterface{
    std::move(instance),
    mode,
    std::move(class_name)};
```

逐项解释：

- `std::move(instance)`：把对象唯一所有权移入返回结构。
- `mode`：复制枚举值，明确记录真实路径。
- `std::move(class_name)`：把已经无需继续使用的字符串移入结果。

随后结果逐层返回：

```text
HybridInterfaceLoader::Load
 -> LoadFromConfig
 -> LoadInterfaceWithModeFromConfig
 -> main 的 loaded 变量
```

至此一次完整加载结束。

---

## 56. 把 A 的静态调用链完整连起来

配置：

```yaml
load_mode: static
class:
  class: ImplA
message: configured-static-A
```

逐步跟踪：

```text
main(argv[1])
  -> LoadInterfaceWithModeFromConfig(file)
  -> 构造 HybridInterfaceLoader
       -> 构造空 registry_
       -> RegisterBuiltInInterfaces
       -> Register<ImplA>("ImplA")
       -> factories_["ImplA"] = &ImplA::NewInstance(Map)
  -> LoadFromConfig(file)
       -> YAML -> Node -> Map
  -> Load(Map)
       -> class_name = "ImplA"
       -> has_file = false
       -> has_version = false
       -> mode = Static
  -> registry_.Create("ImplA", config)
       -> factories_.find("ImplA")
       -> 调用函数指针 factory(config)
       -> ImplA::NewInstance(Map)
       -> 读取 message = configured-static-A
       -> new ImplA(message)
       -> 返回 Loadable*
       -> dynamic_cast<Interface*>
       -> unique_ptr<Interface>
  -> ValidateInterfaceVersion
  -> LoadedInterface{对象, Static, "ImplA"}
  -> main 打印 static
  -> 虚函数调用 ImplA::print/foo/bar
```

静态路径最重要的三个“不发生”：

```text
不读取 class.file
不打开 libimpl_a.so
不经过 token
```

---

## 57. 把 A 的动态调用链完整连起来

配置：

```yaml
load_mode: dynamic
class:
  file: /.../libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-dynamic-A
```

逐步跟踪：

```text
main(argv[1])
  -> LoadInterfaceWithModeFromConfig(file)
  -> 构造 HybridInterfaceLoader
       -> 静态表仍会登记 A/B
       -> 但本次不会使用这些工厂
  -> LoadFromConfig(file)
       -> YAML -> Node -> Map
  -> Load(Map)
       -> class_name = "ImplA"
       -> has_file = true
       -> has_version = true
       -> mode = Dynamic
  -> 检查 class 是 Map、file/ver 完整
  -> higgsops::LoadClass<Interface>(config)
       -> 读取 file/ver/class
       -> 构造 ClassLoader<Interface>
       -> 打开 libimpl_a.so
       -> 检查 HCL_DynamicLibVersion
       -> 找到 ImplA 动态工厂
       -> LoaderHelper 保存完整 Map
       -> 生成 token
       -> ClassLoader::NewInstance(token)
       -> factory(token)
       -> ImplA::NewInstance(token)
       -> GetAssignedConfig(token)
       -> ImplA::NewInstance(Map)
       -> 读取 message = configured-dynamic-A
       -> new ImplA(message)
       -> 返回 Loadable*
       -> dynamic_cast<Interface*>
       -> unique_ptr<Interface>
       -> LoaderHelper 清理 token
  -> ValidateInterfaceVersion
  -> LoadedInterface{对象, Dynamic, "ImplA"}
  -> main 打印 dynamic
  -> 虚函数调用 ImplA::print/foo/bar
```

动态路径虽然在中间绕得更远，但最终和静态路径调用的是同一个：

```cpp
ImplA::NewInstance(const Map&)
```

因此配置默认值、字段校验和构造行为不会分裂成两套。

---

## 58. 两条调用链并排比较

```text
                  静态                              动态

配置              load_mode: static                 load_mode: dynamic
                                                    file + ver + class
                            │                         │
                            └─────────┬───────────────┘
                                      ▼
                           HybridInterfaceLoader
                                      │
                       ┌──────────────┴──────────────┐
                       ▼                             ▼
              registry_.Create              LoadClass<Interface>
                       │                             │
              factories_.find                       ├─ 打开 .so
                       │                             ├─ 检查库版本
                       │                             ├─ 查找工厂
                       │                             └─ 生成 token
                       │                                      │
                       │                       NewInstance(const char*)
                       │                                      │
                       │                         GetAssignedConfig(token)
                       │                                      │
                       └──────────────┬───────────────────────┘
                                      ▼
                         NewInstance(const Map&)
                                      │
                                      ▼
                               new ImplA/ImplB
                                      │
                                      ▼
                         unique_ptr<Interface>
                                      │
                                      ▼
                         统一接口版本检查
                                      │
                                      ▼
                    LoadedInterface(instance, mode, name)
```

要记忆的不是全部函数名，而是三个阶段：

```text
分流前：共同解析配置
分流中：Registry 或 ClassLoader
汇合后：共同使用 Interface
```

---

## 59. 为什么静态表已经注册，动态请求仍会打开 `.so`

运行 dynamic 配置时，loader 构造函数确实仍会执行：

```cpp
RegisterBuiltInInterfaces(registry_);
```

所以 Registry 中已经有 `ImplA`。但随后分支条件是：

```cpp
if (mode == LoadMode::Static) {
    instance = registry_.Create(...);
} else {
    instance = higgsops::LoadClass<Interface>(...);
}
```

dynamic 请求不会“优先使用已经存在的静态 A”，也不会自动降级。它明确跳过 Registry，进入 `.so`。

这很重要，因为否则你无法证明插件更新是否真的生效：程序可能表面成功，实际一直运行内置旧实现。

本项目通过三层手段防止混淆：

```text
配置层：显式 load_mode
返回层：LoadedInterface.mode
测试层：断言 mode 和专属 message
```

---

## 60. “代码在 main 中”和“代码在 so 中”到底是什么意思

### 静态实现

`nm -C main.out` 可以看到：

```text
ImplA::NewInstance(Map const&)
ImplB::NewInstance(Map const&)
```

表示这些函数的机器指令已成为 `main.out` 的一部分。即使删除 `libimpl_a.so`，static A 仍可创建，只要 main 的其他运行库仍齐全。

### 动态实现

`libimpl_a.so` 自己导出：

```text
HCL_DynamicLibVersion
ImplA::NewInstance(char const*)
ImplA::NewInstance(Map const&)
```

dynamic A 需要根据配置找到并打开这个文件。删除或移动 `.so` 后，dynamic A 会失败。

### 为什么 main 中也有 ImplA，so 中也有 ImplA，不冲突

因为它们来自同一源码的两个编译副本：

```text
main.out 中的 ImplA
    服务静态 Registry 创建的对象

libimpl_a.so 中的 ImplA
    服务 ClassLoader 创建的对象
```

两者业务类名相同，但分处不同装载单元。本项目用 `LoadedInterface.mode` 区分对象实际从哪条路径产生。

---

## 61. 建议在 VS Code 中按这个断点顺序单步

不要一开始钻进所有依赖。用 A 的 static 配置先走一遍，再换 dynamic 配置。

### 61.1 静态 A 断点顺序

```text
1. autotest/main.cpp
   LoadInterfaceWithModeFromConfig

2. interface_loader.cpp
   HybridInterfaceLoader::HybridInterfaceLoader

3. builtin_interfaces.cpp
   RegisterBuiltInInterfaces

4. interface_registry.h
   InterfaceRegistry::Register<ImplA>

5. interface_loader.cpp
   HybridInterfaceLoader::LoadFromConfig

6. interface_loader.cpp
   HybridInterfaceLoader::Load

7. interface_loader.cpp
   mode = LoadMode::Static

8. interface_registry.cpp
   InterfaceRegistry::Create

9. impl_a/impl_a.cpp
   NewInstance(const Map&)

10. interface_loader.cpp
    ValidateInterfaceVersion

11. autotest/main.cpp
    loaded.instance->print()
```

这次不应停在：

```cpp
ImplA::NewInstance(const char* token)
```

### 61.2 动态 A 断点顺序

```text
1–6. 与静态相同

7. interface_loader.cpp
   mode = LoadMode::Dynamic

8. interface_loader.cpp
   higgsops::LoadClass<Interface>

9. ConfigFactory.h
   LoaderHelper helper(configNode)

10. ClassLoader.h
    objPtr = factory(objectConfig.c_str())

11. impl_a/impl_a.cpp
    NewInstance(const char* token)

12. ConfigFactory
    GetAssignedConfig(token)

13. impl_a/impl_a.cpp
    NewInstance(const Map&)

14. interface_loader.cpp
    ValidateInterfaceVersion

15. autotest/main.cpp
    loaded.instance->print()
```

动态时应先停在 token 重载，再停在 Map 重载。这两个断点的先后顺序就是“动态适配后汇入共同构造逻辑”的最直观证据。

---

## 62. 用四个问题判断自己是否理解

### 问题 1：静态配置为什么没有 `.a` 路径？

因为 `.a` 只在链接阶段使用。程序运行时，相关机器码已经在 `main.out` 中，不再打开 `.a` 文件。

### 问题 2：动态配置为什么必须有 `.so` 路径？

因为实现代码还没有作为固定插件依赖进入当前加载路径，ClassLoader 必须知道运行时打开哪个文件。

### 问题 3：为什么两个模式最终都返回 `unique_ptr<Interface>`？

因为加载方式只决定对象从哪里来，`Interface` 决定对象创建后怎样被使用。来源不同不应污染业务调用。

### 问题 4：为什么需要 `LoadedInterface.mode`？

因为 A 的静态版本和动态版本可能打印完全相同的业务内容。只有分流器知道真正执行了哪个分支，因此由它把 mode 作为事实返回。

如果这四个问题都能独立回答，核心结构就已经建立起来了。

---

## 63. 最终压缩成可以背下来的十句话

1. `Interface` 规定实现对象创建后能做什么。
2. `ImplA/ImplB` 是满足 `Interface` 的具体实现。
3. CMake 把同一实现源码分别编进静态 `.a` 和动态 `.so`。
4. 静态 `.a` 的实现代码在链接 `main.out` 时进入程序。
5. 动态 `.so` 不作为 A/B 的固定 `NEEDED` 依赖，而是运行时按配置打开。
6. `InterfaceRegistry` 保存“类名到 Map 工厂”的静态映射。
7. `HybridInterfaceLoader` 根据 `load_mode` 选择 Registry 或 ClassLoader。
8. 动态工厂先接收 token，再用 token 取回 Map。
9. 静态和动态最终都进入同一个 `NewInstance(Map)` 构造对象。
10. `LoadedInterface` 同时返回对象和真实模式，让程序与测试明确知道走了哪条路。

最终完整心智模型：

```text
构建时：
同一份实现源码 -> .a + .so

运行时：
同一个 loader -> 每次请求选择 static 或 dynamic

创建时：
不同入口 -> 同一个 Map 工厂 -> 同一种 Interface 对象

使用时：
main 只调用 Interface，不再关心对象来源
```

因此，“最终完成了同时静态/动态加载”准确地说是：

> 当前工程在一次构建中同时准备静态实现和动态插件，同一个可执行程序通过同一个配置加载入口按每次请求选择其中一条创建路径；两条路径共享配置构造和接口校验逻辑，并把实际模式明确返回给调用者。它不是让一个对象同时走两条路径，而是让一个程序无需重新编译就能在两种部署策略之间按配置选择，也能在更大的同一进程中分别创建并持有两种来源的对象。
