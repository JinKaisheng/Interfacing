# Interfacing：基于 Higgs ClassLoader 的 C++ 动态接口加载示例

本项目演示如何使用统一的 C++ 抽象接口、YAML 配置和
`HiggsOps.Interface 2.36.0` 动态加载不同实现。业务代码不直接调用
`dlopen`；新增或切换实现时只修改配置文件，不修改 `main.cpp`。

项目同时实现两层版本保护：

1. Higgs ClassLoader 校验 YAML 中的 `class.ver` 与动态库通过
   `HCL_SO_VERSION` 声明的版本是否一致。
2. 项目加载器校验实现的 `GetVersion()` 是否兼容主程序要求的
   `INTERFACE_VERSION`。

## 已实现功能

- 公共抽象接口 `Interface`，继承 `HiggsIS::Loadable`；
- `ImplA`、`ImplB` 两个独立 `.so` 实现；
- 使用 `higgsops::config::LoadConfigFile` 读取 YAML；
- 使用 `higgsops::LoadClass<Interface>` 创建实现对象；
- 使用 `NewInstance(const char*)` 和 `GetAssignedConfig()` 传递业务配置；
- 使用 `HCL_SO_VERSION` 声明动态库版本；
- 使用 `std::unique_ptr` 管理实现对象生命周期；
- 使用语义化版本规则校验接口兼容性；
- 使用 gtest 覆盖正常加载和版本异常场景；
- 使用 Shannon 公共 CMake/NuGet 工具恢复完整依赖。

## 工作机制

```text
main.out
  -> LoadInterfaceFromConfig(config.yaml)
  -> LoadConfigFile：YAML 转换成 config::Node
  -> Node::AsMap：取得根配置 Map
  -> LoadClass<Interface>
       -> 读取 class.file、class.ver、class.class
       -> 打开指定 .so
       -> 校验 HCL_DynamicLibVersion
       -> 查找 NewInstance(const char*)
       -> 将完整配置暂存并把 token 传给实现
  -> 实现通过 GetAssignedConfig(token) 取回配置
  -> 返回 std::unique_ptr<Interface>
  -> 校验 GetVersion() 与 INTERFACE_VERSION
  -> main 只通过 Interface 调用 print/foo/bar
```

## 目录结构

```text
Interfacing/
├── CMakeLists.txt                 根 CMake 配置
├── compile.sh                    依赖、构建和测试入口
├── dependency.nuspec.in          NuGet 依赖声明
├── interface.h                   公共接口与 INTERFACE_VERSION
├── interface_loader.h/.cpp       配置加载和接口版本校验
├── impl_a/                       ImplA 动态库
├── impl_b/                       ImplB 动态库
├── autotest/                     示例主程序 main.out
├── tests/                        gtest 与不兼容测试插件
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

构建包含测试的版本：

```bash
cmake -S . -B build/server -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=ON
cmake --build build/server -j 4
```

不需要测试时：

```bash
cmake -S . -B build/server -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=OFF
cmake --build build/server -j 4
```

如果 Ninja 输出 `ninja: no work to do.`，表示构建产物已经是最新状态，
不是错误。

主要构建产物：

```text
build/server/bin/main.out
build/server/lib/libimpl_a.so
build/server/lib/libimpl_b.so
build/server/config/impl_a.yaml
build/server/config/impl_b.yaml
```

## 运行

只改变 YAML 参数即可切换实现：

```bash
build/server/bin/main.out build/server/config/impl_a.yaml
build/server/bin/main.out build/server/config/impl_b.yaml
```

生成的配置结构如下：

```yaml
class:
  file: /absolute/path/to/build/server/lib/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-A
```

| 字段 | 含义 |
|---|---|
| `class.file` | 待加载的 `.so` 路径 |
| `class.ver` | 配置期望的动态库版本 |
| `class.class` | 动态库中实现类的完整 C++ 类名 |
| `message` | 传递给具体实现的业务配置 |

`HiggsOps.Interface 2.36.0` 的 `LoadClass` 实际读取
`classInfo["class"]`，因此这里必须使用 `class` 键。

## 运行测试

```bash
cmake -S . -B build/server -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=ON
cmake --build build/server -j 4
ctest --test-dir build/server --output-on-failure
```

当前包含 6 个测试场景：

1. 语义化版本兼容规则；
2. 从配置加载 ImplA；
3. 只改变配置加载 ImplB；
4. 拒绝接口版本为 `0.9.0` 的旧实现；
5. 证明关闭接口校验会让不兼容实现漏过；
6. 拒绝 YAML 声明版本与 `.so` 声明版本不一致。

## 一键脚本

```bash
bash compile.sh deps   # 只恢复依赖
bash compile.sh build  # 只配置并构建
bash compile.sh test   # 只运行测试
bash compile.sh all    # 依赖、构建、测试全部执行
```

默认并行任务数为 4，可通过环境变量覆盖：

```bash
BUILD_JOBS=8 bash compile.sh build
```

## Build 与 Install

```bash
cmake --build build/server -j 4
```

负责把源码编译、链接为 `main.out` 和 `.so`，产物保存在
`build/server/` 构建树中。

```bash
cmake --install build/server
```

负责按照 `install(...)` 规则整理已经构建好的文件。当前项目安装到：

```text
build/install/
├── bin/main.out
├── lib/libinterfacing_loader.a
├── lib/libimpl_a.so
├── lib/libimpl_b.so
└── include/interfacing/
    ├── interface.h
    └── interface_loader.h
```

生成的 YAML 仍位于 `build/server/config/`，当前安装规则不会复制它们。开发和
验收阶段建议直接运行构建树中的程序与配置。

## 插件实现协议

每个实现需要：

1. 继承 `Interface`；
2. 实现 `print()`、`foo()`、`bar()` 和 `GetVersion()`；
3. 声明并在 `.cpp` 类体外定义
   `static HiggsIS::Loadable* NewInstance(const char*)`；
4. 在 `NewInstance()` 中通过 `higgsops::GetAssignedConfig(token)` 取得配置；
5. 在动态库的一个 `.cpp` 中调用一次 `HCL_SO_VERSION("x.y.z")`。

`NewInstance` 应在类体外定义，避免无普通调用点的 inline 工厂函数被优化掉，
导致 ClassLoader 在 `.so` 动态符号表中找不到入口。

检查导出符号：

```bash
nm -D --defined-only build/server/lib/libimpl_a.so \
  | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

## 双层版本校验

### 动态库文件版本

```cpp
HCL_SO_VERSION("1.0.0")
```

该宏导出固定符号 `HCL_DynamicLibVersion`。ClassLoader 将返回值与 YAML
的 `class.ver` 做一致性校验，用于发现配置指向了错误版本的 `.so`。

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

1. 新建 `impl_c/impl_c.cpp` 和 `impl_c/CMakeLists.txt`；
2. 实现 `Interface` 和 ClassLoader 工厂协议；
3. 在根 `CMakeLists.txt` 中加入 `add_subdirectory(impl_c)`；
4. 增加对应 YAML 配置；
5. 增加动态加载和版本测试。

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
ldd build/server/bin/main.out
ldd build/server/lib/libimpl_a.so
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
- [ ] Release 构建成功；
- [ ] `libimpl_a.so`、`libimpl_b.so` 位于 `build/server/lib`；
- [ ] A/B 均可通过各自 YAML 加载；
- [ ] `main.cpp` 不包含具体实现分支；
- [ ] 6 个测试全部通过；
- [ ] `.so` 导出 `NewInstance` 和 `HCL_DynamicLibVersion`；
- [ ] 版本不匹配时能够给出明确错误。
