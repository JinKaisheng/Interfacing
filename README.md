# Interfacing：同时支持静态/动态加载的 C++ 接口示例

本项目演示如何使用统一的 C++ 抽象接口、YAML 配置和
`HiggsOps.Interface 2.36.0` 在同一个主程序中选择静态注册表或动态库加载。
业务代码只调用 `LoadInterfaceWithModeFromConfig()` 和 `Interface`，不会直接
依赖 `ImplA`、`ImplB`，也不直接调用 `dlopen`。

项目同时实现两层版本保护：

1. Higgs ClassLoader 校验 YAML 中的 `class.ver` 与动态库通过
   `HCL_SO_VERSION` 声明的版本是否一致。
2. 项目加载器校验实现的 `GetVersion()` 是否兼容主程序要求的
   `INTERFACE_VERSION`。

第一层只发生在动态路径；静态路径同样执行第二层接口版本校验。

## 已实现功能

- 公共抽象接口 `Interface`，继承 `HiggsIS::Loadable`；
- `ImplA`、`ImplB` 同时生成静态库和动态库；
- `InterfaceRegistry` 注册并创建内置静态实现；
- `HybridInterfaceLoader` 根据 YAML 选择静态或动态路径；
- 使用 `higgsops::config::LoadConfigFile` 读取 YAML；
- 使用 `higgsops::LoadClass<Interface>` 创建实现对象；
- 使用 `NewInstance(const char*)` 和 `GetAssignedConfig()` 传递业务配置；
- 使用 `HCL_SO_VERSION` 声明动态库版本；
- 使用 `std::unique_ptr` 管理实现对象生命周期；
- 使用语义化版本规则校验接口兼容性；
- `LoadedInterface.mode` 明确报告实际加载路径；
- 使用 gtest 覆盖静态、动态、版本和配置异常场景；
- 使用 Shannon 公共 CMake/NuGet 工具恢复完整依赖。

## 工作机制

```text
main.out
  -> LoadInterfaceWithModeFromConfig(config.yaml)
  -> LoadConfigFile：YAML 转换成 config::Node
  -> Node::AsMap：取得根配置 Map
  -> HybridInterfaceLoader::Load
       -> static：InterfaceRegistry::Create(class_name, config)
       -> dynamic：higgsops::LoadClass<Interface>(config)
            -> 打开指定 .so 并校验 HCL_DynamicLibVersion
            -> 调用 NewInstance(const char*)
            -> 插件通过 GetAssignedConfig(token) 取回配置
  -> 校验 GetVersion() 与 INTERFACE_VERSION
  -> 返回 LoadedInterface{instance, mode, class_name}
  -> main 只通过 Interface 调用 print/foo/bar
```

## 目录结构

```text
Interfacing/
├── CMakeLists.txt                 根 CMake 配置
├── compile.sh                    依赖、构建和测试入口
├── dependency.nuspec.in          NuGet 依赖声明
├── interface.h                   公共接口与 INTERFACE_VERSION
├── interface_loader.h/.cpp       静态/动态分派和接口版本校验
├── interface_registry.h/.cpp     静态实现工厂注册表
├── builtin_interfaces.h/.cpp     内置实现注册入口
├── impl_a/                       ImplA 静态库和动态库
├── impl_b/                       ImplB 静态库和动态库
├── autotest/                     示例主程序 main.out
├── tests/                        gtest 与不兼容测试插件
├── .vscode/                      服务器构建和调试任务
├── .agents/skills/               项目收尾维护 Skill
└── py/                           shannon_py_cmake 子模块
```

## 环境要求

推荐环境与已验证服务器环境：

- Linux（当前目标环境为 CentOS 7）；
- GCC/G++ 9.3.1 或兼容的 C++17 编译器；
- CMake 3.14 以上；
- Ninja；
- Python 3.10；
- NuGet 及组内 `cpp17` 软件源；
- 能访问公司 GitHub 和内部 NuGet 仓库。

直接依赖为：

- `HiggsOps.Interface 2.36.0`；
- `gtest [1.12.1, 2.0.0)`。

HiggsOps 还依赖 HiggsIS、spdlog、ZeroMQ、libdeflate 等库。因此不要只复制
一个 `libHiggsOps.so`，应使用依赖脚本恢复完整依赖闭包。

## 从零获取代码

```bash
git clone --recurse-submodules \
  https://github.com/JinKaisheng/Interfacing.git
cd Interfacing
```

如果已经普通克隆，再初始化子模块：

```bash
git submodule update --init --recursive
```

如果子模块使用公司 GitHub SSH 地址，推荐通过 SSH Agent Forwarding 使用本机
密钥，不要把私人密钥复制到服务器。服务器端可检查：

```bash
echo "$SSH_AUTH_SOCK"
ssh-add -l
ssh -T git@github.higgsasset.com
```

## 恢复依赖

```bash
python py/configuration.py dependency.nuspec.in install/
```

恢复完成后检查：

```bash
test -f install/include/higgsops/ConfigFactory.h
test -f install/include/higgsIS/ClassLoader.h
test -f install/lib/libHiggsOps.so
```

这里的 `install/` 是第三方依赖目录，不是当前项目的最终安装结果。

## 配置和构建

调试使用带符号的 Debug 构建：

```bash
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINTERFACING_BUILD_TESTS=ON
cmake --build build/debug -j 4
```

完整验收同时维护 advanced Release 构建：

```bash
cmake -S . -B build/advanced -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=ON
cmake --build build/advanced -j 4
```

如果 Ninja 输出 `ninja: no work to do.`，表示构建产物已经是最新状态，
不是错误。

主要构建产物：

```text
build/debug/bin/main.out
build/debug/bin/interfacing_tests
build/debug/lib/libimpl_ad.so
build/debug/lib/libimpl_bd.so
build/debug/config/impl_a_static.yaml
build/debug/config/impl_a_dynamic.yaml
```

当前 Shannon 工具链给 Debug 库名添加 `d` 后缀；advanced Release 中对应的是
`libimpl_a.so`、`libimpl_b.so`。YAML 由 CMake 根据真实 target 路径生成，不要手改库名。

## 运行

同一个 `main.out` 只通过 YAML 切换静态或动态加载：

```bash
build/debug/bin/main.out build/debug/config/impl_a_static.yaml
build/debug/bin/main.out build/debug/config/impl_a_dynamic.yaml
```

静态配置只提供注册类名和业务字段：

```yaml
load_mode: static
class:
  class: ImplA
message: configured-static-A
```

动态配置还必须提供 `.so` 和插件版本：

```yaml
load_mode: dynamic
class:
  file: /absolute/path/to/build/debug/lib/libimpl_ad.so
  ver: 1.0.0
  class: ImplA
message: configured-dynamic-A
```

| 字段 | 含义 |
|---|---|
| `load_mode` | 明确选择 `static` 或 `dynamic` |
| `class.file` | 待加载的 `.so` 路径 |
| `class.ver` | 配置期望的动态库版本 |
| `class.class` | 动态库中实现类的完整 C++ 类名 |
| `message` | 传递给具体实现的业务配置 |

`HiggsOps.Interface 2.36.0` 的 `LoadClass` 实际读取
`classInfo["class"]`，因此这里必须使用 `class` 键。

## 运行测试

```bash
bash compile.sh debug
bash compile.sh debug-test
bash compile.sh advanced
bash compile.sh advanced-test
```

测试覆盖以下类别：

1. 语义化版本兼容规则；
2. ImplA、ImplB 静态加载并断言 `LoadMode::Static`；
3. ImplA、ImplB 动态加载并断言 `LoadMode::Dynamic`；
4. 接口版本和动态库声明版本异常；
5. 未注册静态类、缺失动态字段和未知模式等配置异常。

## 一键脚本

```bash
bash compile.sh deps           # 修复空缓存、恢复依赖并校验结果
bash compile.sh check-deps     # 只检查关键头文件和库，不访问网络
bash compile.sh debug          # 依赖预检后配置并构建 Debug
bash compile.sh debug-test     # 依赖预检后测试 Debug
bash compile.sh advanced       # 依赖预检后配置并构建 advanced Release
bash compile.sh advanced-test  # 依赖预检后测试 advanced Release
bash .agents/skills/interfacing-maintenance/scripts/verify.sh
```

不传动作时默认执行 Debug 构建，因此 `bash compile.sh` 等价于
`bash compile.sh debug`。普通构建和完整验证只执行本地依赖预检，不会隐式访问内部
NuGet；缺失时会列出文件并提示运行 `bash compile.sh deps`。

显式执行 `deps` 时，脚本会删除确认不包含任何文件或链接的空包缓存，避免上游
恢复脚本把“空目录存在”误判为“已经下载”。恢复结束后还会校验
`ClassLoader.h`、`ConfigFactory.h`、`libHiggsIS.so` 和
`libHiggsOps.so`，确保问题在进入 C++ 编译前暴露。

默认并行任务数为 4，可通过环境变量覆盖：

```bash
BUILD_JOBS=8 bash compile.sh debug
```

## VS Code 构建与调试

`.vscode/tasks.json` 提供依赖检查、Debug、advanced Release、完整验证以及
静态/动态运行任务。新环境可运行 `Interfacing: bootstrap and verify all`，按顺序
恢复依赖并执行完整验证；日常使用 `Interfacing: verify all`，只做本地预检而不联网。
`.vscode/launch.json` 提供主程序和单个 gtest 的静态/动态调试配置；路径使用
`${workspaceFolder}`，因此通过 Remote SSH 打开仓库时不依赖固定账号目录。

代码、CMake、YAML、测试或运行方式改变后，仓库根目录 `AGENTS.md` 要求 Codex
调用项目 Skill `interfacing-maintenance`，检查构建任务、调试任务和本文档，并运行
完整验证。检查并不意味着每次都改文件：只有内容过期时才更新。

## Build 与 Install

```bash
cmake --build build/advanced -j 4
```

负责把源码编译、链接为 `main.out` 和 `.so`，产物保存在
`build/advanced/` 构建树中。

```bash
cmake --install build/advanced
```

负责按照 `install(...)` 规则整理已经构建好的文件。当前项目安装到：

```text
build/install/
├── bin/main.out
├── lib/libinterfacing_loader.a
├── lib/libimpl_a.so
├── lib/libimpl_a_static.a
├── lib/libimpl_b.so
├── lib/libimpl_b_static.a
└── include/interfacing/
    ├── interface.h
    ├── interface_loader.h
    ├── interface_registry.h
    └── builtin_interfaces.h
```

生成的 YAML 仍位于对应构建目录的 `config/`，当前安装规则不会复制它们。开发和
验收阶段建议直接运行构建树中的程序与配置。

## 插件实现协议

每个实现需要：

1. 继承 `Interface`；
2. 实现 `print()`、`foo()`、`bar()` 和 `GetVersion()`；
3. 同时提供 `NewInstance(const char*)` 和 `NewInstance(const Map&)`；
4. token 重载通过 `GetAssignedConfig(token)` 委托给 Map 重载；
5. Map 重载集中完成字段读取和对象构造，供静态、动态路径复用；
6. 为实现生成静态、动态两个 CMake target；
7. 在动态库的一个 `.cpp` 中调用一次 `HCL_SO_VERSION(插件版本宏)`；
8. 在 `RegisterBuiltInInterfaces()` 中注册静态实现。

`NewInstance` 应在类体外定义，避免无普通调用点的 inline 工厂函数被优化掉，
导致 ClassLoader 在 `.so` 动态符号表中找不到入口。

检查导出符号：

```bash
nm -D --defined-only build/debug/lib/libimpl_ad.so \
  | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

## 双层版本校验

### 动态库文件版本

```cpp
// impl_a/plugin_version.cpp
HCL_SO_VERSION(INTERFACING_IMPL_A_PLUGIN_VERSION)
// impl_b/plugin_version.cpp
HCL_SO_VERSION(INTERFACING_IMPL_B_PLUGIN_VERSION)
```

该宏导出固定符号 `HCL_DynamicLibVersion`。ClassLoader 将返回值与 YAML
的 `class.ver` 做一致性校验，用于发现配置指向了错误版本的 `.so`。

插件版本的唯一取值来源是根 `CMakeLists.txt` 中的同名 CMake 变量（A/B 默认均为
`1.0.0`）。`target_compile_definitions(... PRIVATE ...)` 把变量作为字符串宏传给
对应动态库的编译器；生成 YAML 时使用同一个变量，不再使用 `PROJECT_VERSION`。
例如：

```cmake
target_compile_definitions(impl_a PRIVATE
    INTERFACING_IMPL_A_PLUGIN_VERSION="${INTERFACING_IMPL_A_PLUGIN_VERSION}")
```

编译时等价于在该 target 的源文件前定义：

```cpp
#define INTERFACING_IMPL_A_PLUGIN_VERSION "1.0.0"
```

这些是 target 私有宏，不会把 ImplA 的插件版本传播给主程序或 ImplB。
宏最终仍展开为字符串；本次改造消除的是分散的版本字面量，而不是改变 HCL 协议。
A/B 变量为 CMake 缓存项，修改已配置构建树时需显式传入
`-DINTERFACING_IMPL_A_PLUGIN_VERSION=新版本`（或 B 对应参数）后重新构建；仅修改
`set(... CACHE ...)` 的默认值不会覆盖旧缓存。插件产品版本变化不会自动改变
`Interface::GetVersion()`。

| 版本职责 | 定义来源 | 使用位置 |
|---|---|---|
| 主程序及 A/B 的接口契约 | `interface.h` 的 `INTERFACE_VERSION` | `GetVersion()` 与加载器最低要求 |
| ImplA 插件产品版本 | `INTERFACING_IMPL_A_PLUGIN_VERSION` | A 的 HCL 导出、A 的动态 YAML（含别名） |
| ImplB 插件产品版本 | `INTERFACING_IMPL_B_PLUGIN_VERSION` | B 的 HCL 导出、B 的动态 YAML（含别名） |
| Legacy 插件产品版本 | `tests/CMakeLists.txt` 的 `INTERFACING_LEGACY_PLUGIN_VERSION` | Legacy 的 HCL 导出与 YAML |
| Legacy 接口契约 | `GetVersion()` 保留固定返回 `"0.9.0"` | 维持原有旧接口负例语义 |
| 项目发布版本 | CMake `PROJECT_VERSION` | 项目发布标识，不再决定插件 YAML 版本 |

Legacy 的 HCL 宏和原有接口返回值当前都为 `0.9.0`，保持“文件版本通过、接口版本
拒绝”的测试语义。`GetVersion()` 和 Interface 契约逻辑不属于本次重构的修改范围。
测试中的错误输入（如 `9.9.9`）与独立期望值仍保留字面量，避免测试跟着实现的
同一个宏一起出错。版本宏只是命名，不提供额外的 ABI 安全保证。

### Interface 接口版本

`interface.h` 定义：

```cpp
#define INTERFACE_VERSION "1.0.0"
```

实现通过 `GetVersion()` 返回其编译时接口版本。兼容规则为：

```text
implementation.major == required.major
并且
implementation >= required
```

例如主程序最低要求 `1.2.3`：

| 实现版本 | 结果 | 原因 |
|---|---|---|
| `1.2.3` | 接受 | 完全一致 |
| `1.2.4` | 接受 | 同 major 的 patch 更新 |
| `1.3.0` | 接受 | 同 major 的 minor 更新 |
| `1.2.2` | 拒绝 | 低于最低要求 |
| `2.0.0` | 拒绝 | major 不同，可能存在破坏性变化 |

该规则依赖团队遵守语义化版本约定：同一 major 内的新实现必须向后兼容，
破坏兼容性的修改必须升级 major。

## 新增实现

新增 `ImplC` 时：

1. 新建 `impl_c/impl_c.h`、`impl_c/impl_c.cpp`、版本源文件和 CMake 配置；
2. 实现 `Interface`、token 工厂和 Map 工厂；
3. 在根 `CMakeLists.txt` 中加入 `add_subdirectory(impl_c)`；
4. 在 `RegisterBuiltInInterfaces()` 中注册静态工厂；
5. 同时生成静态、动态 YAML 配置；
6. 增加静态、动态加载和版本测试。

`main.cpp` 不应为具体实现增加 `if/else` 分支。实现选择必须由 YAML 完成，
这是插件化设计的核心。

## 常见问题

### `Permission denied (publickey)`

这是 Git SSH 认证失败，不是 CMake 问题。检查 `ssh-agent`、公司 GitHub
登记的公钥以及 Agent Forwarding。

### `interface_loader.h: No such file or directory`

确认文件位于项目根目录，并注意 Linux 文件名大小写：

```bash
ls -l interface_loader.h
```

### `undefined reference`

检查 `install/lib` 中的完整依赖、`target_link_libraries()` 和链接顺序。

### 运行时找不到 `.so`

```bash
ldd build/debug/bin/main.out
ldd build/debug/lib/libimpl_ad.so
```

项目已将 `install/lib` 加入构建 RPATH，不建议先用全局 `LD_LIBRARY_PATH`
掩盖缺失依赖问题。

### YAML 版本不一致

首先保证：

```text
YAML class.ver == 动态库 HCL_SO_VERSION
```

这与 `GetVersion()` 的 Interface 兼容性校验是两个不同层次。

## 验收清单

- [ ] `py` 子模块已初始化；
- [ ] `install/include/higgsops/ConfigFactory.h` 存在；
- [ ] `install/lib/libHiggsOps.so` 存在；
- [ ] Debug 和 advanced Release 构建成功；
- [ ] Debug 和 advanced Release 的全部 CTest 测试通过；
- [ ] A/B 均覆盖静态和动态加载；
- [ ] 主程序分别输出 `via static mode` 和 `via dynamic mode`；
- [ ] `main.cpp` 不包含具体实现分支；
- [ ] `.so` 导出 `NewInstance` 和 `HCL_DynamicLibVersion`；
- [ ] 版本不匹配时能够给出明确错误。

## 延伸文档

- [从零到一实现记录](FROM_ZERO_TO_ONE.md)
- [从 GitHub 基础版到进阶版的逐步手册](GUIDE_GITHUB_BASIC_TO_ADVANCED.md)
- [开发命令与排障速查](docs/DEVELOPMENT_COMMAND_REFERENCE.md)
