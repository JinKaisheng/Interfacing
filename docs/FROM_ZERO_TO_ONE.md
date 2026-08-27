# Interfacing 进阶作业：从 0 到 1 的原理、实现与复刻手册

> 构建目录说明：本文包含从零复刻时使用的早期 `build/server` 命令。当前高阶版
> 正式使用 `build/debug` 和 `build/advanced`；当前构建、调试和验收命令以
> `README.md` 和 `docs/DEVELOPMENT_COMMAND_REFERENCE.md` 为准。

本文档回答三个问题：

1. 这次实际做了哪些操作，为什么这样做；
2. `HiggsOps.Interface + LoadClass + YAML + .so` 整套机制如何工作；
3. 如果不依赖现有构建结果，如何从空目录手工复刻并在服务器运行。

本文对应的本地工程是 `C:\LocalCode\Interfacing`，服务器工程是
`/home/jinkaisheng/Interfacing`。当前验证环境为 CentOS 7、GCC 9.3.1、
CMake 3.22、Ninja、Python 3.10。

---

## 一、最终完成了什么

基础作业原先是下面这条链路：

```text
main.cpp
  -> 手工 dlopen(libimpl_a.so)
  -> dlsym(create_instance)
  -> Interface*
```

现在改成了进阶要求的链路：

```text
main.cpp
  -> 读取 YAML
  -> LoadInterfaceWithModeFromConfig()
  -> higgsops::LoadClass<Interface>()
  -> HiggsIS::ClassLoader
  -> 校验 HCL_SO_VERSION
  -> 调用实现类的静态 NewInstance()
  -> dynamic_cast<Interface*>
  -> 校验 Interface 语义版本
  -> std::unique_ptr<Interface>
```

目前具备以下能力：

- `main.cpp` 中没有 `dlopen`、`dlsym` 或实现 A/B 的头文件；
- 修改 YAML 就能在 `ImplA` 与 `ImplB` 之间切换；
- 实现类可以读取与自身同一 YAML 节点中的业务配置；
- 动态库自身版本由 `HCL_SO_VERSION` 标记；
- Interface 版本由 `INTERFACE_VERSION` 和 `GetVersion()` 标记；
- 加载时进行两层版本检查；
- 使用 Shannon 公共 CMake/依赖管理结构；
- 使用 gtest 覆盖动态加载和版本错误场景；
- 已在 `10.214.2.51` 上完成依赖安装、编译和运行；
- 服务器测试结果为 6/6 通过。

---

## 二、这次实际执行过的行为

### 1. 阅读和定位工程

首先检查了以下内容：

- `shannon_it_public/interface/README.md` 中的基础、进阶和高级要求；
- `Interfacing` 中原有的 Interface、A/B 实现、CMake 和 `dlopen` 主程序；
- `shannon_sample_cpp` 的统一工程结构；
- `HiggsOps.Interface.2.36.0` 的 nuspec、头文件、动态库和 CMake props；
- `py/configuration.py`、`py/higgs_nuget.py` 的依赖安装逻辑。

最开始给出的路径中写成了 `C:\LocalCode\shannon\...`，实际目录名是
`C:\LocalCode\shannon_it_public\...`。确认真实目录后才继续操作。

同时检查了 `git status`，没有覆盖或清理已有改动，也没有替用户提交 Git。

### 2. 确认 HiggsOps 不是一个可单独使用的目录

读取 `HiggsOps.Interface.2.36.0/HiggsOps.Interface.nuspec` 后发现它还依赖：

```text
HiggsIS          [1.9.2, 2.0.0)
spdlog-linux     [1.8.2]
libzmq-linux     [4.3.3]
libdeflate       [1.12.0]
```

使用 `readelf -d libHiggsOps.so` 又确认了运行时依赖，包括：

```text
libHiggsIS.so
libspdlog.so.1
libzmq.so.5
libdeflate.so.0
libcurl.so.4
libz.so.1
libstdc++.so.6
```

因此，“把 `HiggsOps.Interface.2.36.0/include` 和一个 `.so` 塞进工程”并不能
构成完整依赖环境。正确入口应是项目级 `dependency.nuspec.in`。

### 3. 检查本机和服务器环境

本机的 `HiggsOps` 文件是 Linux ELF 库，Windows 进程不能直接加载。虽然本机
有 WSL，但 WSL 是 Ubuntu 26.04、GCC 15，而目标包和组内工程主要面向 CentOS 7
或 Rocky Linux 8。因此选择直接在服务器上构建最稳妥。

第一次 SSH 探测失败，原因不是服务器不可用，而是 SSH 配置写了不存在的私钥：

```text
IdentityFile C:/Users/jinka/.ssh/id_rsa
```

实际存在的是：

```text
C:/Users/jinka/.ssh/id_ed25519
```

修正 `C:\Users\jinka\.ssh\config` 后，下面的命令可以免密登录：

```bash
ssh jinkaisheng@10.214.2.51
```

服务器检查结果：

```text
OS       CentOS 7
GCC      9.3.1
CMake    3.22.0
Python   3.10.9
NuGet    已安装并配置公司 cpp17 源
Ninja    已安装
Git      已安装
```

### 4. 建立可重复的依赖声明

新增 `dependency.nuspec.in`，直接依赖只有两项：

```xml
<dependencies>
  <dependency id="HiggsOps.Interface" version="[2.36.0]" />
  <dependency id="gtest" version="[1.12.1, 2.0.0)" />
</dependencies>
```

然后在服务器运行：

```bash
python py/configuration.py dependency.nuspec.in install/
```

脚本从 NuGet 下载了直接依赖和传递依赖，并生成：

```text
install/include/          合并后的头文件入口
install/lib/              当前系统对应的库入口
install/packages/         各原始 NuGet 包
install/dependency.json   包之间的依赖图
install/used_package.json 最终选用的包版本
```

服务器是 CentOS 7，所以使用包中的 `lib/`。Rocky Linux 8 应使用包中的
`lib.rockylinux8/`，公共脚本会根据 `/etc/os-release` 选择。

### 5. 阅读真正的 LoadClass 契约

依赖安装完成后，读取了：

```text
install/include/higgsops/ConfigFactory.h
install/include/higgsIS/ClassLoader.h
```

确认了三个重要事实：

1. Interface 必须继承 `HiggsIS::Loadable`；
2. 实现类必须提供 `static Loadable* NewInstance(const char*)`；
3. `LoadClass` 实际读取的是 `classInfo["class"]`。

`ConfigFactory.h` 附近的旧注释写的是 `name: test`，但真实代码是：

```cpp
classInfo["file"].AsString();
classInfo["ver"].AsString();
classInfo["class"].AsString();
```

所以 YAML 必须写 `class:`，不能照旧注释写 `name:`。

### 6. 改造 Interface、实现和主程序

主要代码行为如下：

- `Interface` 继承 `HiggsIS::Loadable`；
- 增加 `INTERFACE_VERSION "1.0.0"`；
- 增加虚函数 `GetVersion()`；
- A/B 实现增加静态 `NewInstance()`；
- A/B 动态库分别使用自己的插件产品版本宏，Interface 契约仍使用
  `INTERFACE_VERSION`；
- 实现使用 `higgsops::GetAssignedConfig(token)` 读取自身配置；
- 新增统一加载函数 `LoadInterfaceWithModeFromConfig()`，返回对象、真实模式和类名；
- `main.cpp` 只接收配置文件路径并调用统一加载函数；
- 删除主程序中的手工 `dlopen`/`dlsym` 逻辑。

### 7. 改造成 Shannon 统一 CMake 工程

根 CMake 接入：

```cmake
set(shannon_py_cmake_path "${CMAKE_CURRENT_SOURCE_DIR}/py")
set(HIGGS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/install")
set(CMAKE_MODULE_PATH "${shannon_py_cmake_path}/cmake/modules" ${CMAKE_MODULE_PATH})

include(higgs_common)
include(ShannonCommon)
shannon_init_compiler_system()
shannon_release_with_info()
```

同时链接 HiggsOps 的直接运行依赖，并为服务器构建目录设置 RPATH，使生成的
程序无需修改系统 `/etc/ld.so.conf` 就可以找到 `install/lib`。

CMake 使用 `file(GENERATE)` 生成配置文件，这样 `$<TARGET_FILE:impl_a>` 会被
替换成构建后动态库的真实绝对路径。

### 8. 编译过程中实际解决的错误

第一次编译遇到两个普通源码错误：

- `GetAssignedConfig` 实际在 `higgsops`，不是 `higgsops::config`；
- README 要求的 `GetVersion()` 是非 const，校验函数不能接收 `const Interface&`。

修正后能够链接，但第一次动态加载仍失败。使用下面的命令检查：

```bash
nm -D --defined-only build/libimpl_a.so | c++filt
```

结果只有 `HCL_DynamicLibVersion`，没有 `ImplA::NewInstance(char const*)`。

根因是 `NewInstance` 原先直接定义在类体内，它天然是 inline；由于工程内部没有
普通 C++ 调用点，优化器没有把这个符号输出到 `.so`。ClassLoader 运行时再找它
已经来不及了。

解决方法是类内只声明，类外定义：

```cpp
class ImplA final : public Interface {
public:
    static HiggsIS::Loadable* NewInstance(const char* config_token);
};

HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    // ...
}
```

再次用 `nm -D` 验证后，可以同时看到：

```text
HCL_DynamicLibVersion
ImplA::NewInstance(char const*)
```

这一步是整个进阶作业最容易忽略的 ABI/动态符号问题。

### 9. 增加测试并完成服务器验证

增加了 6 个测试：

1. 语义版本规则；
2. 通过配置加载 ImplA；
3. 只改变配置加载 ImplB；
4. 拒绝 0.9.0 的不兼容实现；
5. 关闭校验后证明 0.9.0 会漏过；
6. 拒绝 YAML 中错误声明的动态库版本 9.9.9。

最终服务器结果：

```text
100% tests passed, 0 tests failed out of 6
```

实际运行结果：

```text
Loaded interface version 1.0.0
Implementation A [configured-A] - print()
Implementation A - foo()
Implementation A - bar()
```

切换到 B 时 `main.cpp` 不变，只传入另一个 YAML。

### 10. 增加可维护性文件

最后补充了：

- `compile.sh`：依赖、构建、测试的一键入口；
- `.vscode/tasks.json`：VS Code 任务；
- `.vscode/settings.json`：CMake Tools 构建目录；
- `.vscode/extensions.json`：推荐扩展；
- `.gitignore`：忽略 `build/`、`install/` 和本地虚拟环境；
- `README.md`：日常使用速查。

并通过 VS Code Remote-SSH 打开了服务器上的工程目录。

---

## 三、整个动态加载机制应该怎样理解

### 1. Interface 是主程序和插件之间的 ABI 合同

主程序不知道 `ImplA`、`ImplB` 的完整类型。它只知道：

```cpp
class Interface : public HiggsIS::Loadable {
public:
    virtual ~Interface() = default;
    virtual void print() = 0;
    virtual void foo() = 0;
    virtual void bar() = 0;
    virtual std::string GetVersion() = 0;
};
```

这份头文件不是普通的“编译提示”，而是二进制层面的合同，包括：

- 基类继承关系；
- 虚函数数量；
- 虚函数顺序；
- 参数和返回类型；
- const/noexcept 等函数修饰；
- C++ ABI 和标准库 ABI。

主程序和所有插件必须基于兼容的 Interface 编译。随意在虚函数表中间插入函数，
可能让旧插件仍能被加载，但函数调用跳到错误地址，这也是必须进行版本检查的原因。

虚析构函数保证 `std::unique_ptr<Interface>` 删除插件对象时会调用实现类析构函数。

### 2. YAML 是“选择哪个实现”的数据

典型配置：

```yaml
class:
  file: /home/user/project/build/server/lib/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-A
```

字段含义：

| 字段 | 含义 |
|---|---|
| `class.file` | 要加载的动态库文件 |
| `class.ver` | 期望动态库通过 `HCL_SO_VERSION` 声明的版本 |
| `class.class` | 动态库中的完整 C++ 类名；有命名空间时必须写完整名 |
| 其他字段 | 传给具体实现的业务配置 |

新增插件时，主程序无需再增加 `if (name == "A")`。选择行为被移到了配置层。

### 3. LoadClass 做了什么

`higgsops::LoadClass<T>` 的核心可以理解为：

```cpp
template<typename T>
std::unique_ptr<T> LoadClass(const config::Map& node) {
    const auto& class_info = node["class"].AsMap();

    HiggsIS::ClassLoader<T> loader(
        class_info["file"].AsString(),
        class_info["ver"].AsString(),
        class_info["class"].AsString());

    LoaderHelper helper(node);
    return loader.NewInstance(helper.token.data());
}
```

可按下面的顺序理解：

```text
解析 class.file
  -> 打开 .so
  -> 查找 HCL_DynamicLibVersion
  -> 对比 class.ver
  -> 根据 class.class 解析 NewInstance 入口
  -> 暂存整个配置节点并生成 token
  -> NewInstance(token)
  -> 实现通过 GetAssignedConfig(token) 取回配置
  -> 返回 HiggsIS::Loadable*
  -> dynamic_cast<T*>
  -> std::unique_ptr<T>
```

基础设施内部仍然需要操作动态库，但这些细节被封装在 ClassLoader 中。作业所说的
“不能直接使用 `dlopen`”，指业务代码必须依赖这个统一协议，而不是每个项目自己
定义一套 `create_instance`/`destroy_instance`。

### 4. token 为什么存在

ClassLoader 规定工厂入口统一为：

```cpp
static HiggsIS::Loadable* NewInstance(const char* token);
```

它没有为每个业务设计不同参数。`LoaderHelper` 先把 YAML 节点暂存在基础设施中，
然后传递一个短 token。插件调用：

```cpp
const auto config = higgsops::GetAssignedConfig(token);
```

即可取回属于本次实例化的完整配置。这既保持了工厂 ABI 固定，也允许每个实现拥有
不同的业务配置。

### 5. HCL_SO_VERSION 为什么必须导出

宏：

```cpp
HCL_SO_VERSION("1.0.0")
```

会导出一个 C 符号：

```text
HCL_DynamicLibVersion
```

ClassLoader 可以在创建对象之前读取它，并与 YAML 的 `class.ver` 比较。如果配置写
9.9.9，而动态库实际声明 1.0.0，加载会在实例化前失败。

它校验的是“配置所说的动态库版本”和“文件自身声明的版本”，并不能完全代替
Interface ABI 兼容性检查。

### 6. 为什么还需要 GetVersion

两层校验关注点不同：

```text
HCL_SO_VERSION:
YAML 声明的插件版本 == .so 自身声明的插件版本

GetVersion:
插件编译时使用的 Interface 版本 是否兼容 主程序编译时的 Interface 版本
```

本项目采用的 Interface 兼容规则是：

```text
implementation.major == required.major
并且
implementation >= required
```

示例：

| 主程序需要 | 插件接口版本 | 结果 |
|---|---:|---|
| 1.0.0 | 1.0.0 | 兼容 |
| 1.0.0 | 1.2.0 | 兼容，假定 minor 向后兼容 |
| 1.1.0 | 1.0.9 | 不兼容，插件太旧 |
| 1.0.0 | 2.0.0 | 不兼容，major 破坏性变化 |

如果团队的版本规范要求更严格，可以改成完全相等，但必须让规则显式化、可测试，
不能依靠“看起来版本差不多”。

### 7. 编译期、链接期、运行期依赖是三件事

这是理解依赖错误的关键。

#### 编译期

编译器需要：

```text
install/include/higgsops/ConfigFactory.h
install/include/higgsIS/ClassLoader.h
install/include/gtest/...
```

缺少时出现 `file not found`、类型未声明等错误。

#### 链接期

链接器需要：

```text
install/lib/libHiggsOps.so
install/lib/libHiggsIS.so
install/lib/libspdlog.so
...
```

缺少时出现 `cannot find -lHiggsOps` 或 `undefined reference`。

#### 运行期

Linux 动态加载器还必须能再次找到这些 `.so`。可通过：

- ELF 的 RPATH/RUNPATH；
- `LD_LIBRARY_PATH`；
- 系统 `/etc/ld.so.conf`；
- 系统默认库目录。

本项目的构建产物使用 `CMAKE_BUILD_RPATH` 指向工程的 `install/lib`，测试还显式
设置 `LD_LIBRARY_PATH`，因此无需修改服务器全局配置。

可以用下面的命令检查运行期解析：

```bash
ldd build/server/bin/main.out
ldd build/server/lib/libimpl_a.so
```

任何 `not found` 都说明是运行期库搜索问题，不是 C++ 头文件问题。

---

## 四、从空目录手工复刻

下面假定在 Windows 使用 VS Code 编辑，在 CentOS 7 服务器编译运行。

### 第 0 步：准备权限和工具

确认 Windows 上存在私钥：

```powershell
Get-ChildItem C:\Users\你的用户名\.ssh
```

SSH 配置示例：

```sshconfig
Host 10.214.2.51
  HostName 10.214.2.51
  User jinkaisheng
  Port 22
  IdentityFile "C:/Users/你的用户名/.ssh/id_ed25519"
  ForwardAgent yes
```

测试：

```powershell
ssh -o BatchMode=yes jinkaisheng@10.214.2.51 "uname -a"
```

服务器检查：

```bash
gcc --version
g++ --version
cmake --version
ninja --version
python --version
nuget sources List
```

### 第 1 步：创建工程和 py 子模块

```bash
mkdir Interfacing
cd Interfacing
git init
git submodule add git@github.higgsasset.com:Shannon/shannon_py_cmake.git py

mkdir impl_a impl_b autotest tests
```

如果代码已在 Git 仓库中，则使用：

```bash
git clone --recurse-submodules <你的仓库地址> Interfacing
```

漏掉 `--recurse-submodules` 时可以补：

```bash
git submodule update --init --recursive
```

### 第 2 步：写 dependency.nuspec.in

最小依赖声明：

```xml
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2010/07/nuspec.xsd">
  <metadata>
    <id>Interfacing</id>
    <version>1.0.0</version>
    <authors>your-name</authors>
    <owners>your-name</owners>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <description>Interface loading exercise</description>
    <dependencies>
      <dependency id="HiggsOps.Interface" version="[2.36.0]" />
      <dependency id="gtest" version="[1.12.1, 2.0.0)" />
    </dependencies>
  </metadata>
</package>
```

安装：

```bash
python py/configuration.py dependency.nuspec.in install/
```

验证：

```bash
test -f install/include/higgsops/ConfigFactory.h
test -f install/include/higgsIS/ClassLoader.h
test -f install/lib/libHiggsOps.so
test -f install/lib/libHiggsIS.so
cat install/used_package.json
```

此处失败时先处理 NuGet 源、网络或包版本问题，不要进入 C++ 编码阶段。

### 第 3 步：定义 interface.h

```cpp
#pragma once

#include <higgsIS/ClassLoader.h>
#include <string>

#define INTERFACE_VERSION "1.0.0"

class Interface : public HiggsIS::Loadable {
public:
    ~Interface() override = default;
    virtual void print() = 0;
    virtual void foo() = 0;
    virtual void bar() = 0;
    virtual std::string GetVersion() = 0;
};
```

先冻结这份接口，再编译主程序和所有实现。不要让不同实现各自复制一份内容不一致的
`interface.h`。

### 第 4 步：实现 ImplA

核心结构：

```cpp
#include "interface.h"
#include <higgsops/ConfigFactory.h>

class ImplA final : public Interface {
public:
    explicit ImplA(std::string message) : message_(std::move(message)) {}

    void print() override;
    void foo() override;
    void bar() override;
    std::string GetVersion() override { return INTERFACE_VERSION; }

    static HiggsIS::Loadable* NewInstance(const char* token);

private:
    std::string message_;
};

HiggsIS::Loadable* ImplA::NewInstance(const char* token) {
    const higgsops::config::Map config = higgsops::GetAssignedConfig(token);
    return new ImplA(config.GetOrDefault("message", "default-A"));
}

HCL_SO_VERSION(INTERFACING_IMPL_A_PLUGIN_VERSION)
```

`INTERFACING_IMPL_A_PLUGIN_VERSION` 由 ImplA 动态 target 的
`target_compile_definitions` 提供；根 CMake 用同一个版本变量生成 YAML。
它不是 `INTERFACE_VERSION` 的别名。

注意：`NewInstance` 必须返回 `HiggsIS::Loadable*`，而不是为了方便写成
`ImplA*` 或 `Interface*`。这是 ClassLoader 头文件明确规定的工厂 ABI。

按同样方式实现 `ImplB`，类名、输出内容和默认配置不同即可。

### 第 5 步：写统一加载入口

`interface_loader.cpp` 负责：

```cpp
higgsops::config::Node root =
    higgsops::config::LoadConfigFile(config_file);

std::unique_ptr<Interface> instance =
    higgsops::LoadClass<Interface>(root.AsMap());

ValidateInterfaceVersion(*instance);
return instance;
```

版本解析和兼容规则也放在这里，而不是散落在每个调用点。这样所有业务入口默认都会
校验版本，不容易有人忘记。

### 第 6 步：写 main.cpp

主程序只负责业务流程：

```cpp
int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }

    LoadedInterface loaded =
        LoadInterfaceWithModeFromConfig(argv[1]);
    loaded.instance->print();
    loaded.instance->foo();
    loaded.instance->bar();
}
```

主程序不应包含：

```text
ImplA.h
ImplB.h
dlopen
dlsym
create_instance
destroy_instance
```

### 第 7 步：写 CMake

需要完成四件事：

1. 接入 Shannon 公共 CMake；
2. 把 `install/include` 加入头文件搜索路径；
3. 把 `install/lib` 加入链接和运行时搜索路径；
4. 构建 loader、A/B `.so`、main 和 tests。

核心配置：

```cmake
cmake_minimum_required(VERSION 3.14)
project(Interfacing VERSION 1.0.0 LANGUAGES CXX)

set(shannon_py_cmake_path "${CMAKE_CURRENT_SOURCE_DIR}/py")
set(HIGGS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/install")
set(CMAKE_MODULE_PATH "${shannon_py_cmake_path}/cmake/modules" ${CMAKE_MODULE_PATH})

include(higgs_common)
include(ShannonCommon)
shannon_init_compiler_system()
shannon_release_with_info()

set(CMAKE_CXX_STANDARD 17)
link_directories("${HIGGS_INSTALL_DIR}/lib")
set(CMAKE_BUILD_RPATH "${HIGGS_INSTALL_DIR}/lib")

set(HIGGS_LIBRARIES
    HiggsOps HiggsIS spdlog deflate zmq pgm-5.2 Threads::Threads dl)
```

动态库目标：

```cmake
add_library(impl_a SHARED impl_a.cpp)
target_link_libraries(impl_a PRIVATE ${HIGGS_LIBRARIES})
```

主程序：

```cmake
add_library(interfacing_loader STATIC interface_loader.cpp)
target_link_libraries(interfacing_loader PUBLIC ${HIGGS_LIBRARIES})

add_executable(main.out main.cpp)
target_link_libraries(main.out PRIVATE interfacing_loader)
```

生成 YAML：

```cmake
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/config/impl_a.yaml" CONTENT
"class:\n  file: $<TARGET_FILE:impl_a>\n  ver: ${INTERFACING_IMPL_A_PLUGIN_VERSION}\n  class: ImplA\nmessage: configured-A\n")
```

完整可工作的 CMake 请直接对照当前仓库，而不是只复制以上节选。

### 第 8 步：先编译，再检查动态符号

```bash
cmake -S . -B build/server -G Ninja
cmake --build build/server -j 4
```

在运行主程序前检查插件：

```bash
nm -D --defined-only build/server/lib/libimpl_a.so | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

必须同时看见版本符号和工厂符号。

检查库依赖：

```bash
ldd build/server/lib/libimpl_a.so | grep 'not found'
ldd build/server/bin/main.out | grep 'not found'
```

没有输出才表示没有缺失的运行库。

### 第 9 步：运行

```bash
build/server/bin/main.out build/server/config/impl_a.yaml
build/server/bin/main.out build/server/config/impl_b.yaml
```

两条命令使用同一个二进制。若增加 `ImplC`，主程序仍然不应修改。

### 第 10 步：添加测试

至少测试：

- A 能成功加载；
- B 能成功加载；
- A/B 切换只依赖配置；
- 相同 major 且实现版本不低于调用方时通过；
- 旧版本实现被拒绝；
- major 不同被拒绝；
- YAML 声明版本与 `HCL_SO_VERSION` 不一致时被拒绝；
- 明确关闭版本校验时，不兼容对象会漏过。

运行：

```bash
ctest --test-dir build/server --output-on-failure
```

最后一个“关闭校验”的测试不是推荐用法，而是为了证明校验机制确实有价值。

---

## 五、如何新增第三个实现而不修改 main.cpp

假设新增 `ImplC`：

1. 新建 `impl_c/impl_c.cpp`；
2. 继承 `Interface`；
3. 实现所有虚函数；
4. 类外定义 `ImplC::NewInstance`；
5. 为 ImplC 定义独立的 `INTERFACING_IMPL_C_PLUGIN_VERSION`；
6. 添加 `HCL_SO_VERSION(INTERFACING_IMPL_C_PLUGIN_VERSION)`，并在 CMake 中添加
   `add_library(impl_c SHARED ...)`；
7. 新建或生成 `impl_c.yaml`；
8. 运行同一个 `main.out impl_c.yaml`。

YAML 示例：

```yaml
class:
  file: /path/to/libimpl_c.so
  ver: 1.0.0
  class: my_namespace::ImplC
message: configured-C
```

如果第 8 步需要修改 `main.cpp`，说明选择实现的逻辑仍然没有完全配置化。

---

## 六、迁移到另一台服务器

推荐迁移源码和依赖声明，不要把当前 `build/` 当作唯一交付物。

### 方式 A：从 Git 克隆

```bash
git clone --recurse-submodules <repo-url> Interfacing
cd Interfacing
python py/configuration.py dependency.nuspec.in install/
cmake -S . -B build/server -G Ninja
cmake --build build/server -j 4
ctest --test-dir build/server --output-on-failure
```

### 方式 B：复制工作区

复制这些内容：

```text
CMakeLists.txt
dependency.nuspec.in
interface*.h / interface*.cpp
impl_a/ impl_b/ autotest/ tests/
py/
```

通常不复制：

```text
build/
install/
.venv/
```

在目标服务器重新恢复依赖、重新配置和编译。CMake 生成的 YAML 中包含绝对动态库
路径，因此换机器或换目录后必须重新运行 CMake，不能直接沿用旧 YAML。

目标服务器还必须满足：

- 能访问公司 NuGet/Nexus；
- NuGet 源和认证已配置；
- CPU 架构一致；
- 操作系统与选用的库目录一致；
- GCC/libstdc++ ABI 与依赖包兼容。

---

## 七、VS Code 中的日常操作

### 打开远端工程

1. 安装 `Remote - SSH`；
2. `Ctrl+Shift+P`；
3. 执行 `Remote-SSH: Connect to Host...`；
4. 选择 `10.214.2.51`；
5. 打开 `/home/jinkaisheng/Interfacing`。

左下角应显示远端 SSH 主机。此时集成终端、CMake、IntelliSense 和测试均运行在
CentOS 服务器，不是在 Windows 上尝试加载 Linux `.so`。

### 运行预置任务

执行 `Terminal -> Run Task`：

```text
Interfacing: dependencies
Interfacing: build
Interfacing: test
Interfacing: all
Interfacing: run A
Interfacing: run B
```

默认构建任务是 `Interfacing: all`。

---

## 八、常见错误与定位顺序

| 现象 | 常见原因 | 检查方法 |
|---|---|---|
| SSH 报 `no such identity` | `IdentityFile` 路径错误 | 查看 `~/.ssh/config` 和实际密钥文件名 |
| 找不到 `ConfigFactory.h` | 没恢复依赖或 include 路径错误 | `test -f install/include/higgsops/ConfigFactory.h` |
| `cannot find -lHiggsOps` | `install/lib` 未生成或 link path 错 | `ls install/lib/libHiggsOps.so` |
| 运行时报 `.so: cannot open` | RPATH/`LD_LIBRARY_PATH` 问题 | `ldd main.out` |
| `ClassNotFoundException` | 文件、版本、类名或工厂符号错误 | 依次检查 YAML、`nm -D`、HCL 版本 |
| 配置写了 `name` 仍加载失败 | 2.36.0 实际读取 `class` | 改为 `class.class` |
| 有 HCL 版本符号但没有 NewInstance | 工厂定义在类内被优化掉 | 类内声明、类外定义，再用 `nm -D` 验证 |
| Windows 无法加载 `.so` | `.so` 是 Linux ELF | 在 Remote-SSH 服务器或匹配的 Linux 环境运行 |
| CentOS 能编译、Rocky 运行异常 | 选错 NuGet 库目录 | CentOS 用 `lib/`，Rocky 8 用 `lib.rockylinux8/` |
| 改了目录后 YAML 指向旧路径 | YAML 由 CMake 写入绝对路径 | 重新运行 CMake configure |
| 版本明显不匹配却仍运行 | 绕过了统一加载/校验入口 | 禁止业务代码直接构造或关闭校验 |

推荐排错顺序：

```text
SSH/系统环境
  -> NuGet 下载
  -> install/include
  -> install/lib
  -> C++ 编译
  -> 链接
  -> ldd 运行库
  -> YAML 文件路径
  -> HCL_SO_VERSION
  -> NewInstance 动态符号
  -> class 完整名称
  -> Interface 版本
```

不要一开始就反复修改 C++。依赖下载、链接、运行库搜索、动态符号和 Interface ABI
是五个不同层次，先判断错误属于哪一层。

---

## 九、最终自检清单

### 依赖

- [ ] `dependency.nuspec.in` 固定或限制了合理版本；
- [ ] `configuration.py` 能从空 `install/` 恢复依赖；
- [ ] `used_package.json` 中没有意外版本；
- [ ] OS 对应的 `lib` 目录选择正确。

### 插件 ABI

- [ ] Interface 继承 `HiggsIS::Loadable`；
- [ ] Interface 有虚析构函数；
- [ ] 所有实现覆盖全部虚函数；
- [ ] `NewInstance` 返回 `HiggsIS::Loadable*`；
- [ ] `NewInstance` 在类外定义；
- [ ] `.so` 导出 `NewInstance`；
- [ ] `.so` 导出 `HCL_DynamicLibVersion`。

### 配置和加载

- [ ] YAML 使用 `class.file`、`class.ver`、`class.class`；
- [ ] 类名包含完整命名空间；
- [ ] 实现通过 `GetAssignedConfig` 获取配置；
- [ ] 主程序只依赖 Interface 和统一 loader；
- [ ] 主程序没有直接 `dlopen`。

### 版本

- [ ] `INTERFACE_VERSION` 已定义；
- [ ] 每个实现正确返回其编译时接口版本；
- [ ] HCL 动态库版本校验开启；
- [ ] Interface 兼容规则已编码并测试；
- [ ] 不兼容场景会在业务调用前失败。

### 构建和运行

- [ ] `cmake --build` 成功；
- [ ] `ldd` 没有 `not found`；
- [ ] `nm -D` 能找到两个关键符号；
- [ ] A/B 都能运行；
- [ ] `ctest` 全部通过；
- [ ] 换目录后重新生成配置文件。

---

## 十、当前工程的关键文件索引

| 文件 | 作用 |
|---|---|
| `dependency.nuspec.in` | 声明 HiggsOps 和 gtest 依赖 |
| `interface.h` | Interface ABI 和版本号 |
| `interface_loader.h/.cpp` | LoadClass 封装和版本规则 |
| `impl_a/impl_a.cpp` | 动态实现 A |
| `impl_b/impl_b.cpp` | 动态实现 B |
| `autotest/main.cpp` | 不感知具体实现的主程序 |
| `tests/legacy_impl.cpp` | 模拟旧版本实现 |
| `tests/interface_tests.cpp` | 动态加载和版本测试 |
| `CMakeLists.txt` | 统一构建入口和配置生成 |
| `compile.sh` | 依赖、构建、测试的一键入口 |
| `.vscode/tasks.json` | VS Code 远端任务 |
| `README.md` | 日常速查 |

日常使用看 `README.md`；需要理解原理、从零重建或排错时看本文档。
