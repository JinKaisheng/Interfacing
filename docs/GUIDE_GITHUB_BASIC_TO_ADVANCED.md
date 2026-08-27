# 从 GitHub 基础版到 HiggsOps 进阶版：逐步教学手册

> 构建目录说明：本文按演进阶段保留了早期 `build/server` 示例。当前高阶版正式
> 使用 `build/debug` 和 `build/advanced`；不要把历史命令当成当前入口，当前操作
> 请以 `README.md` 和 `docs/DEVELOPMENT_COMMAND_REFERENCE.md` 为准。

本手册把以下仓库作为唯一代码起点：

```text
https://github.com/JinKaisheng/Interfacing.git
```

基础版固定为提交：

```text
13b51cb  commit: Interface-based plugin architecture
```

即使以后 `main` 分支发生变化，也可以从这个提交重复本实验。

本手册假设：

- 服务器上没有 `Interfacing` 目录；
- 服务器上没有本项目的 `install/` 和 `build/`；
- 你不使用任何之前生成的动态库或配置文件；
- 你亲自创建每个进阶文件并理解原因；
- 编译运行环境是 `jinkaisheng@10.214.2.51` 的 CentOS 7；
- 本机使用 VS Code 1.98.2，通过 Remote-SSH 连接服务器。

如果你现在服务器上已有同名目录，不要删除它。把本手册中的实验目录改成
`~/Interfacing-from-zero` 即可。

---

## 1. 你最终要完成什么

基础版的调用链是：

```text
命令行传入 libimpl_a.so
    ↓
main.cpp 直接调用 dlopen
    ↓
dlsym("create_instance")
    ↓
Interface*
```

进阶版的调用链是：

```text
命令行传入 impl_a.yaml
    ↓
higgsops::config::LoadConfigFile
    ↓
higgsops::LoadClass<Interface>
    ↓
HiggsIS::ClassLoader
    ├─ 打开 class.file 指定的 .so
    ├─ 校验 HCL_SO_VERSION
    ├─ 按 class.class 查找静态 NewInstance
    └─ 将配置 token 传给 NewInstance
    ↓
实现类调用 higgsops::GetAssignedConfig(token)
    ↓
返回 std::unique_ptr<Interface>
    ↓
校验 Interface 版本
    ↓
执行业务虚函数
```

最终验收目标：

- 主程序不出现 `dlopen`、`dlsym`、`ImplA` 或 `ImplB`；
- A/B 的切换只改变 YAML 路径；
- 实现可以读取 YAML 中自己的配置；
- 错误动态库版本在实例化前被拒绝；
- 错误 Interface 版本在业务调用前被拒绝；
- gtest 覆盖成功与失败场景；
- 从空目录执行依赖恢复、构建、测试后全部通过。

---

## 2. 理解四个层次

动手前先区分四件容易混淆的事。

### 2.1 源代码依赖

编译器需要找到：

```text
higgsops/ConfigFactory.h
higgsIS/ClassLoader.h
gtest/gtest.h
```

找不到时通常报：

```text
fatal error: xxx.h: No such file or directory
```

### 2.2 链接依赖

链接器需要：

```text
libHiggsOps.so
libHiggsIS.so
libspdlog.so
libdeflate.so
libzmq.so
libpgm-5.2.so
```

找不到时通常报：

```text
cannot find -lHiggsOps
undefined reference to ...
```

### 2.3 运行时依赖

程序启动后，Linux 动态加载器必须再次找到上述 `.so`。

找不到时通常报：

```text
error while loading shared libraries: xxx.so: cannot open shared object file
```

检查命令：

```bash
ldd build/server/bin/main.out
ldd build/server/lib/libimpl_a.so
```

### 2.4 插件 ABI 合同

主程序与插件必须对 `Interface` 的二进制结构有一致理解，包括：

- 继承关系；
- 虚函数数量和顺序；
- 参数和返回类型；
- const/noexcept 修饰；
- C++ ABI 与标准库 ABI。

这不是普通的“头文件能编译就行”。接口错位可能导致加载成功、调用时才崩溃，
所以需要专门的版本校验。

---

## 3. 第 0 阶段：连接服务器并检查环境

### 3.1 在本机测试 SSH

PowerShell 中执行：

```powershell
ssh jinkaisheng@10.214.2.51
```

如果出现平台选择，选择 `Linux`。

SSH 配置应类似：

```sshconfig
Host 10.214.2.51
  HostName 10.214.2.51
  User jinkaisheng
  Port 22
  IdentityFile "C:/Users/jinka/.ssh/id_ed25519"
  ForwardAgent yes
```

原因：Remote-SSH 和普通终端最终都依赖同一个 OpenSSH 配置。先保证普通 SSH
成功，再排查 VS Code，可以避免把密钥问题误认为 VS Code 问题。

### 3.2 用 VS Code 打开远端目录

1. 启动 VS Code 1.98.2；
2. `Ctrl+Shift+P`；
3. 选择 `Remote-SSH: Connect to Host...`；
4. 选择 `10.214.2.51`；
5. 连接后选择 `Terminal -> New Terminal`。

注意：当前服务器是 CentOS 7，只能使用最后支持旧 glibc 的 VS Code 1.98 系列。
不要让 VS Code 或 Remote-SSH 自动升级。

### 3.3 检查服务器工具

在远端终端执行：

```bash
uname -a
cat /etc/os-release
g++ --version
cmake --version
ninja --version
python --version
nuget sources List
git --version
```

预期关键结果：

```text
CentOS 7
GCC 9.3.1 左右
CMake 3.14+
Python 3.8+
Ninja 可用
NuGet 中存在 cpp17 公司源
```

原因：

- 项目使用 C++17；
- Shannon 示例要求 CMake 3.14 起；
- `py/configuration.py` 需要 Python 3.8+；
- HiggsOps 包来自公司的 NuGet/Nexus，而不是系统 yum。

如果 `nuget sources List` 没有公司源，不要继续。先向组内确认 Nexus 地址、账号和
认证方式，避免手工下载一半依赖后进入不可复现状态。

---

## 4. 第 1 阶段：从 GitHub 基础提交开始

### 4.1 克隆仓库

如果服务器确实没有旧目录：

```bash
cd ~
git clone https://github.com/JinKaisheng/Interfacing.git
cd Interfacing
```

如果已有工作目录，用独立教学目录：

```bash
cd ~
git clone https://github.com/JinKaisheng/Interfacing.git Interfacing-from-zero
cd Interfacing-from-zero
```

### 4.2 固定基础提交并新建分支

```bash
git checkout 13b51cb
git switch -c feature/higgsops-advanced
```

验证：

```bash
git status
git log --oneline -3
find . -maxdepth 3 -type f | sort
```

此时应只有这些业务文件：

```text
CMakeLists.txt
README.md
interface.h
impl_a/CMakeLists.txt
impl_a/impl_a.cpp
impl_b/CMakeLists.txt
impl_b/impl_b.cpp
autotest/CMakeLists.txt
autotest/main.cpp
```

原因：固定提交能保证教程不会受未来 `main` 分支变化影响；新建 feature 分支能保留
基础版作为对照，也方便按阶段提交。

---

## 5. 第 2 阶段：先验证基础版

不要一克隆就重写代码。先证明基础版在当前服务器能运行。

```bash
cmake -S . -B build/basic -G Ninja
cmake --build build/basic -j 4

build/basic/bin/main.out build/basic/lib/libimpl_a.so
build/basic/bin/main.out build/basic/lib/libimpl_b.so
```

验证点：

- 生成 `libimpl_a.so`、`libimpl_b.so`；
- 同一个 `main.out` 能加载两者；
- 当前实现确实依赖 `dlopen`、`dlsym`、`create_instance`。

原因：如果基础版都不能运行，问题可能是编译器、CMake 或系统环境，而不是
HiggsOps。建立可运行基线后，每次改造才有明确对照。

可以查看当前动态符号：

```bash
nm -D --defined-only build/basic/lib/libimpl_a.so | c++filt
```

此时应看到：

```text
create_instance
destroy_instance
```

---

## 6. 第 3 阶段：接入 Shannon 公共工程工具

### 6.1 添加 py 子模块

```bash
git submodule add \
  git@github.higgsasset.com:Shannon/shannon_py_cmake.git py
git submodule status
```

如果内部 GitHub SSH 没有权限，先测试：

```bash
ssh -T git@github.higgsasset.com
```

原因：`py` 不是随便复制的一组脚本，而是独立版本化的公共构建组件。使用 Git
submodule 才能记录本项目依赖的是公共工具的哪个提交。

以后克隆带子模块的进阶仓库时应使用：

```bash
git clone --recurse-submodules <repo-url>
```

或补执行：

```bash
git submodule update --init --recursive
```

### 6.2 新建 .gitignore

```gitignore
build/
install/
.venv/
Interfacing.nuspec
*.nupkg
```

原因：

- `build/` 是可重新生成的编译产物；
- `install/` 是依赖恢复结果，体积大且可能包含平台差异；
- 真正需要提交的是依赖声明，不是下载结果。

### 6.3 建议提交一次

```bash
git add .gitmodules py .gitignore
git commit -m "build: add Shannon CMake tooling"
```

原因：把工程基础设施和业务逻辑拆成不同提交，后续回看更容易理解。

---

## 7. 第 4 阶段：声明并恢复依赖

### 7.1 为什么不能只复制 HiggsOps.Interface 目录

`HiggsOps.Interface 2.36.0` 自己还依赖：

```text
HiggsIS 1.9.2
spdlog-linux 1.8.2
libzmq-linux 4.3.3
libdeflate 1.12.0
```

`libHiggsOps.so` 的动态依赖还包括 `libcurl`、`libz`、`libstdc++` 等。

所以正确做法是声明一个依赖根，让公共脚本递归解析；错误做法是手工复制几份
头文件和 `.so`，直到链接器暂时不报错。

### 7.2 新建 dependency.nuspec.in

在仓库根目录创建：

```xml
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2010/07/nuspec.xsd">
  <metadata>
    <id>Interfacing</id>
    <version>1.0.0</version>
    <authors>jinkaisheng</authors>
    <owners>jinkaisheng</owners>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <description>Interface-based static and dynamic loading exercise.</description>
    <dependencies>
      <dependency id="HiggsOps.Interface" version="[2.36.0]" />
      <dependency id="gtest" version="[1.12.1, 2.0.0)" />
    </dependencies>
  </metadata>
</package>
```

版本号解释：

- `[2.36.0]` 表示精确使用 2.36.0，保证实验可重复；
- `[1.12.1, 2.0.0)` 表示 gtest 至少 1.12.1、低于 2.0.0；
- 本项目最终会实际选择左边界 1.12.1。

### 7.3 恢复依赖

```bash
python py/configuration.py dependency.nuspec.in install/
```

完成后检查：

```bash
test -f install/include/higgsops/ConfigFactory.h
test -f install/include/higgsIS/ClassLoader.h
test -f install/lib/libHiggsOps.so
test -f install/lib/libHiggsIS.so
test -f install/lib/libgtest.a

cat install/used_package.json
cat install/dependency.json
```

目录含义：

```text
install/packages/         NuGet 原始包目录
install/include/          合并后的编译期头文件入口
install/lib/              当前 OS 对应的库入口
install/dependency.json   依赖关系图
install/used_package.json 最终解析出的版本
```

CentOS 7 使用 NuGet 包中的 `lib/`；Rocky Linux 8 使用
`lib.rockylinux8/`。脚本会读取 `/etc/os-release` 自动判断。

### 7.4 为什么 Git 仓库中看不到 HiggsOps.Interface.2.36.0

这是有意设计，不是遗漏。

Git 仓库应提交的是“依赖声明和恢复工具”：

```text
dependency.nuspec.in   声明需要 HiggsOps.Interface 2.36.0
.gitmodules            记录 Shannon 公共工具子模块
py                     指向公共工具的固定 Git 提交
CMakeLists.txt          从 install/include 和 install/lib 使用依赖
```

Git 仓库不应提交：

```text
HiggsOps.Interface.2.36.0/
install/packages/HiggsOps.Interface.2.36.0/
install/include/higgsops/
install/include/higgsIS/
install/lib/libHiggsOps.so
install/lib/libHiggsOps-static.a
其他由 NuGet 下载的头文件、.so 和 .a
```

原因有四个：

1. `HiggsOps.Interface` 来自公司内部 Nexus，不应复制进公开 GitHub；
2. 二进制和传递依赖体积大，不适合由 Git 版本控制；
3. CentOS 7 与 Rocky Linux 8 使用不同库目录，提交其中一套会造成平台错误；
4. `dependency.nuspec.in` 才是可审查、可升级、可重复的依赖来源。

执行依赖恢复后，2.36.0 包实际位于本机工作区：

```text
install/packages/HiggsOps.Interface.2.36.0/
```

公共脚本还会把当前操作系统需要的内容安装或链接到统一入口：

```text
install/include/higgsops/
install/include/higgsIS/
install/lib/libHiggsOps.so
install/lib/libHiggsIS.so
```

代码只引用统一入口，不直接引用带版本号的包目录。这样升级版本时只需要修改
`dependency.nuspec.in` 并重新恢复依赖，C++ include 路径无需改变。

用下面的命令验证 Git 的边界：

```bash
grep -n 'HiggsOps.Interface' dependency.nuspec.in
git check-ignore -v install/lib/libHiggsOps.so
git status --short
```

预期：

- 第一条显示精确版本 `[2.36.0]`；
- 第二条显示 `install/` 被 `.gitignore` 忽略；
- 第三条不会列出任何 `install/` 内容。

不要执行类似下面的操作：

```bash
# 错误示例，不要执行
cp -r /某处/HiggsOps.Interface.2.36.0 .
git add HiggsOps.Interface.2.36.0
```

### 7.5 进入 Interface 改造前的阶段门槛

进入第 8 节前，必须让以下检查全部成功：

```bash
set -e
test -f dependency.nuspec.in
test -f py/configuration.py
test -f install/include/higgsops/ConfigFactory.h
test -f install/include/higgsIS/ClassLoader.h
test -f install/lib/libHiggsOps.so
test -f install/lib/libHiggsIS.so
test -f install/lib/libgtest.a
echo "dependency gate passed"
```

如果任何一条失败，应停留在第 7 阶段，重新执行：

```bash
python py/configuration.py dependency.nuspec.in install/
```

不要通过把包复制进 Git 仓库来绕过失败。依赖恢复失败通常属于 Nexus 源、认证、
网络、Python 环境或包版本问题，应在依赖层解决。

### 7.6 查看真正的加载契约

不要只看 README 注释，直接阅读头文件：

```bash
sed -n '35,100p' install/include/higgsIS/ClassLoader.h
sed -n '300,350p' install/include/higgsops/ConfigFactory.h
```

你应该确认：

- `Interface` 需要继承 `HiggsIS::Loadable`；
- 实现需要静态 `NewInstance(const char*)`；
- `NewInstance` 必须返回 `HiggsIS::Loadable*`；
- `LoadClass` 读取 `file`、`ver`、`class`；
- 旧注释里的 `name` 与真实实现不一致，不能使用。

### 7.7 建议提交一次

```bash
git add dependency.nuspec.in
git commit -m "build: declare HiggsOps and gtest dependencies"
```

---

## 8. 第 5 阶段：升级 Interface ABI

将 `interface.h` 替换为：

```cpp
#ifndef INTERFACING_INTERFACE_H
#define INTERFACING_INTERFACE_H

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

#endif
```

每项原因：

- 继承 `Loadable`：让 `ClassLoader` 的工厂返回值可以安全 `dynamic_cast`；
- 虚析构：允许通过 `unique_ptr<Interface>` 正确析构插件对象；
- `INTERFACE_VERSION`：记录主程序编译时的 ABI 版本；
- `GetVersion()`：让加载后的实现声明自己基于哪个 Interface 编译。

版本规则采用：

```text
实现 major == 主程序 major
并且实现版本 >= 主程序需要的版本
```

含义：同 major 下允许向后兼容的 minor/patch 更新，拒绝旧实现和破坏性 major
更新。如果团队规范要求完全相等，只需修改规则和测试，不能隐式猜测。

---

## 9. 第 6 阶段：建立统一加载和版本校验层

### 9.1 新建 interface_loader.h

```cpp
#ifndef INTERFACING_INTERFACE_LOADER_H
#define INTERFACING_INTERFACE_LOADER_H

#include "interface.h"

#include <memory>
#include <string>

bool IsInterfaceVersionCompatible(const std::string& required,
                                  const std::string& implementation);

void ValidateInterfaceVersion(Interface& instance);

enum class LoadMode {
    Static,
    Dynamic,
};

struct LoadedInterface {
    std::unique_ptr<Interface> instance;
    LoadMode mode;
    std::string class_name;
};

LoadedInterface LoadInterfaceWithModeFromConfig(
    const std::string& config_file,
    bool validate_version = true);

#endif
```

### 9.2 新建 interface_loader.cpp

```cpp
#include "interface_loader.h"

#include <higgsops/ConfigFactory.h>

#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

using Version = std::array<unsigned int, 3>;

Version ParseVersion(const std::string& text) {
    Version result{};
    std::string_view remaining(text);

    for (std::size_t index = 0; index < result.size(); ++index) {
        if (remaining.empty()) {
            throw std::invalid_argument(
                "version must use MAJOR.MINOR.PATCH: " + text);
        }

        const char* begin = remaining.data();
        const char* end = begin + remaining.size();
        const auto parsed = std::from_chars(begin, end, result[index]);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) {
            throw std::invalid_argument(
                "invalid numeric version component: " + text);
        }

        if (index + 1 == result.size()) {
            if (parsed.ptr != end) {
                throw std::invalid_argument(
                    "version must use MAJOR.MINOR.PATCH: " + text);
            }
        } else {
            if (parsed.ptr == end || *parsed.ptr != '.') {
                throw std::invalid_argument(
                    "version must use MAJOR.MINOR.PATCH: " + text);
            }
            remaining.remove_prefix(
                static_cast<std::size_t>(parsed.ptr - begin) + 1);
        }
    }

    return result;
}

}  // namespace

bool IsInterfaceVersionCompatible(const std::string& required,
                                  const std::string& implementation) {
    const Version required_version = ParseVersion(required);
    const Version implementation_version = ParseVersion(implementation);
    return required_version[0] == implementation_version[0] &&
           implementation_version >= required_version;
}

void ValidateInterfaceVersion(Interface& instance) {
    const std::string implementation_version = instance.GetVersion();
    if (!IsInterfaceVersionCompatible(
            INTERFACE_VERSION, implementation_version)) {
        throw std::runtime_error(
            "Interface version mismatch: consumer=" INTERFACE_VERSION
            ", implementation=" + implementation_version);
    }
}

LoadedInterface LoadInterfaceWithModeFromConfig(
    const std::string& config_file,
    bool validate_version) {
    higgsops::config::Node root =
        higgsops::config::LoadConfigFile(config_file);

    const higgsops::config::Map config = root.AsMap();
    const std::string class_name =
        config["class"].AsMap()["class"].AsString();

    std::unique_ptr<Interface> instance =
        higgsops::LoadClass<Interface>(config);

    if (validate_version) {
        ValidateInterfaceVersion(*instance);
    }
    return LoadedInterface{
        std::move(instance), LoadMode::Dynamic, class_name};
}
```

原因：所有调用点都必须走同一个入口。若把版本判断直接写在 `main.cpp`，以后另一个
程序调用 `LoadClass` 时很容易忘记校验。

`validate_version=false` 只用于测试“绕过校验的风险”，生产代码不要传 false。

---

## 10. 第 7 阶段：将实现改为 Higgs ClassLoader 协议

### 10.1 改造 ImplA

`impl_a/impl_a.cpp`：

```cpp
#include "interface.h"

#include <higgsops/ConfigFactory.h>

#include <iostream>
#include <string>
#include <utility>

class ImplA final : public Interface {
public:
    explicit ImplA(std::string message)
        : message_(std::move(message)) {}

    void print() override {
        std::cout << "Implementation A [" << message_
                  << "] - print()" << std::endl;
    }

    void foo() override {
        std::cout << "Implementation A - foo()" << std::endl;
    }

    void bar() override {
        std::cout << "Implementation A - bar()" << std::endl;
    }

    std::string GetVersion() override {
        return INTERFACE_VERSION;
    }

    static HiggsIS::Loadable* NewInstance(const char* config_token);

private:
    std::string message_;
};

HiggsIS::Loadable* ImplA::NewInstance(const char* config_token) {
    const higgsops::config::Map config =
        higgsops::GetAssignedConfig(config_token);
    return new ImplA(config.GetOrDefault("message", "default-A"));
}

HCL_SO_VERSION(INTERFACE_VERSION)
```

必须理解的三点：

1. `NewInstance` 返回 `HiggsIS::Loadable*`，这是固定工厂 ABI；
2. token 不是业务配置本身，而是基础设施暂存配置后的索引；
3. `NewInstance` 必须类内声明、类外定义。

第三点非常重要。若直接在类体内定义，函数天然是 inline；由于普通代码没有调用
点，优化器可能不把它导出到动态符号表，运行时 ClassLoader 就找不到它。

### 10.2 改造 ImplB

结构与 A 完全相同，只改变类名、输出和默认消息：

```cpp
class ImplB final : public Interface {
public:
    explicit ImplB(std::string message)
        : message_(std::move(message)) {}

    void print() override {
        std::cout << "Implementation B [" << message_
                  << "] - print()" << std::endl;
    }
    void foo() override {
        std::cout << "Implementation B - foo()" << std::endl;
    }
    void bar() override {
        std::cout << "Implementation B - bar()" << std::endl;
    }
    std::string GetVersion() override {
        return INTERFACE_VERSION;
    }

    static HiggsIS::Loadable* NewInstance(const char* config_token);

private:
    std::string message_;
};

HiggsIS::Loadable* ImplB::NewInstance(const char* config_token) {
    const higgsops::config::Map config =
        higgsops::GetAssignedConfig(config_token);
    return new ImplB(config.GetOrDefault("message", "default-B"));
}

HCL_SO_VERSION(INTERFACE_VERSION)
```

文件顶部需要与 A 相同的 include。

### 10.3 不再需要旧工厂函数

删除：

```cpp
extern "C" Interface* create_instance();
extern "C" void destroy_instance(Interface*);
```

原因：这些是基础版自定义协议。进阶版应统一使用 Higgs ClassLoader 的
`NewInstance + HCL_SO_VERSION` 协议。

---

## 11. 第 8 阶段：主程序只认识配置和 Interface

将 `autotest/main.cpp` 替换为：

```cpp
#include "interface_loader.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <config.yaml>" << std::endl;
        return 1;
    }

    try {
        LoadedInterface loaded =
            LoadInterfaceWithModeFromConfig(argv[1]);

        std::cout << "Loaded " << loaded.class_name << std::endl;
        std::cout << "Loaded interface version "
                  << loaded.instance->GetVersion() << std::endl;
        loaded.instance->print();
        loaded.instance->foo();
        loaded.instance->bar();
        return 0;
    } catch (const HiggsIS::Exception& error) {
        std::cerr << "Failed to load implementation: "
                  << error.GetMessage() << std::endl;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Failed to load implementation: "
                  << error.what() << std::endl;
        return 2;
    }
}
```

为什么捕获两类异常：`HiggsIS::Exception` 不是 `std::exception` 的子类，需要单独
处理；版本校验使用的是标准异常。

检查主程序是否彻底解耦：

```bash
grep -nE 'dlopen|dlsym|ImplA|ImplB|create_instance' autotest/main.cpp
```

预期没有输出。

---

## 12. 第 9 阶段：改造 CMake

这一阶段最容易卡住，是因为 CMake 同时涉及源码、编译器、链接器、动态库和运行时
加载器。先建立一个最重要的认识：

> CMake 不是编译器，也不负责下载 HiggsOps。CMake 读取工程描述，生成一张“构建
> 关系图”，再让 Make/Ninja 调用编译器和链接器完成真正的构建。

### 12.0 先理解完整流水线

本项目从依赖声明到程序运行一共经过以下步骤：

```text
dependency.nuspec.in
        │
        │ python py/configuration.py dependency.nuspec.in install/
        ▼
install/include + install/lib              依赖恢复阶段
        │
        │ cmake -S . -B build/server
        ▼
CMake 读取根目录及各子目录 CMakeLists.txt  配置/生成阶段
        │
        │ cmake --build build/server
        ▼
build/server/lib/libimpl_a.so
build/server/lib/libimpl_b.so              编译/链接阶段
build/server/bin/main.out
build/server/config/*.yaml
        │
        │ main.out 读取 YAML
        ▼
HiggsIS 按 class.file 动态加载指定 .so    运行阶段
```

必须区分下面五件事：

| 阶段 | 命令或组件 | 产生的结果 |
| --- | --- | --- |
| 恢复依赖 | `configuration.py` | `install/include`、`install/lib` |
| 配置/生成 | `cmake -S . -B build/server` | `CMakeCache.txt`、Makefile/Ninja 规则、YAML |
| 构建 | `cmake --build build/server` | `.o`、`.a`、`.so`、可执行程序 |
| 运行 | Linux 动态加载器和 HiggsIS | 启动程序并按 YAML 加载实现 |
| 安装 | `cmake --install build/server` | 可选地把本项目产物复制到安装目录 |

本项目的构建依赖图是：

```text
HiggsOps/HiggsIS/spdlog/... ─┬─> interfacing_loader.a ─> main.out
                             ├─> libimpl_a.so
                             └─> libimpl_b.so
```

运行时关系则是：

```text
main.out
   └─> interfacing_loader
          └─> 读取 YAML 中的 class.file
                 ├─> libimpl_a.so
                 └─> libimpl_b.so
```

因此 `main.out` 在构建时不链接 `impl_a`、`impl_b`。它只链接 loader，具体实现由
YAML 在运行时选择。这正是“主程序只认识配置和 Interface”的构建层表达。

还要区分两个名字相似的目录：

```text
项目/install/                外部依赖输入，由 configuration.py 生成
项目/build/server/           CMake 构建目录，可删除后重新生成
项目/build/install/          执行 cmake --install 后的本项目安装输出
```

### 12.1 根 CMake 的职责

根 `CMakeLists.txt` 需要：

- 接入 `py` 公共模块；
- 将 `install/include` 加入编译路径；
- 将 `install/lib` 加入链接路径；
- 使用 C++17；
- 配置 RPATH；
- 构建 loader、A、B、main 和 tests；
- 生成包含真实 `.so` 路径的 YAML。

完整结构：

```cmake
cmake_minimum_required(VERSION 3.14)
project(Interfacing VERSION 1.0.0 LANGUAGES CXX)

set(shannon_py_cmake_path "${CMAKE_CURRENT_SOURCE_DIR}/py")
set(HIGGS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/install"
    CACHE PATH "Higgs dependency installation directory")
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/build/install"
    CACHE PATH "Install prefix" FORCE)
set(CMAKE_MODULE_PATH
    "${shannon_py_cmake_path}/cmake/modules" ${CMAKE_MODULE_PATH})

include(higgs_common)
include(ShannonCommon)
shannon_init_compiler_system()
if(NOT WIN32)
    shannon_release_with_info()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(BEFORE "${CMAKE_CURRENT_SOURCE_DIR}")
link_directories("${HIGGS_INSTALL_DIR}/lib")

set(CMAKE_BUILD_RPATH "${HIGGS_INSTALL_DIR}/lib")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
add_link_options("-Wl,-rpath-link,${HIGGS_INSTALL_DIR}/lib")

set(INTERFACING_HIGGS_LIBRARIES
    HiggsOps
    HiggsIS
    spdlog
    deflate
    zmq
    pgm-5.2
    Threads::Threads
    dl
)

add_library(interfacing_loader STATIC interface_loader.cpp)
target_include_directories(interfacing_loader
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(interfacing_loader
    PUBLIC ${INTERFACING_HIGGS_LIBRARIES})

add_subdirectory(impl_a)
add_subdirectory(impl_b)
add_subdirectory(autotest)

# 在首次实现动态加载、尚未创建 tests/ 时暂设为 OFF。
# 完成第 14 节后把默认值改为 ON。
option(INTERFACING_BUILD_TESTS "Build Interfacing tests" OFF)
if(INTERFACING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/config")
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/config/impl_a.yaml"
    CONTENT
"class:\n  file: $<TARGET_FILE:impl_a>\n  ver: ${PROJECT_VERSION}\n  class: ImplA\nmessage: configured-A\n")
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/config/impl_b.yaml"
    CONTENT
"class:\n  file: $<TARGET_FILE:impl_b>\n  ver: ${PROJECT_VERSION}\n  class: ImplB\nmessage: configured-B\n")

install(TARGETS interfacing_loader
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
install(FILES interface.h interface_loader.h
    DESTINATION include/interfacing)
```

下面按功能解释根文件中的关键语句。

#### 12.1.1 项目与路径变量

```cmake
cmake_minimum_required(VERSION 3.14)
project(Interfacing VERSION 1.0.0 LANGUAGES CXX)
```

这两句声明最低 CMake 版本、项目名称、项目版本和使用的语言。后面 YAML 中的
`${PROJECT_VERSION}` 就会展开成 `1.0.0`。

```cmake
set(HIGGS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/install"
    CACHE PATH "Higgs dependency installation directory")
```

`CMAKE_CURRENT_SOURCE_DIR` 是当前 `CMakeLists.txt` 所在的源码目录。`CACHE PATH`
表示该值会写入构建目录的 `CMakeCache.txt`，也允许在命令行覆盖：

```bash
cmake -S . -B build/server \
  -DHIGGS_INSTALL_DIR=/其他位置/install
```

如果修改路径后 CMake 仍然使用旧值，首先检查：

```bash
grep HIGGS_INSTALL_DIR build/server/CMakeCache.txt
```

#### 12.1.2 `include()` 与 C++ `#include` 完全不同

```cmake
set(CMAKE_MODULE_PATH
    "${shannon_py_cmake_path}/cmake/modules" ${CMAKE_MODULE_PATH})
include(higgs_common)
include(ShannonCommon)
```

这里的 `include()` 是在**配置阶段加载另一个 CMake 文件**。设置
`CMAKE_MODULE_PATH` 后，CMake 才能找到：

```text
py/cmake/modules/higgs_common.cmake
py/cmake/modules/ShannonCommon.cmake
```

而 C++ 中的：

```cpp
#include <higgsops/ConfigFactory.h>
```

是在**编译阶段加载头文件**。二者只是名字相似，工作阶段和作用都不同。

`higgs_common.cmake` 内部执行了近似逻辑：

```cmake
if(IS_DIRECTORY ${HIGGS_INSTALL_DIR}/include)
    include_directories(${HIGGS_INSTALL_DIR}/include)
endif()
```

所以根 CMake 虽然没有直接写 `install/include`，编译器最后仍能找到：

```text
install/include/higgsops/ConfigFactory.h
install/include/higgsIS/ClassLoader.h
```

这是公司公共模块带来的隐式行为，不是 CMake 自动猜测依赖位置。

#### 12.1.3 编译语言和位置无关代码

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
```

- 使用标准 C++17；
- 编译器不支持 C++17 时直接报错；
- 不依赖 `gnu++17` 特有扩展；
- 生成位置无关代码，Linux 中通常对应 `-fPIC`，构建 `.so` 时需要。

#### 12.1.4 头文件搜索与库搜索

```cmake
include_directories(BEFORE "${CMAKE_CURRENT_SOURCE_DIR}")
link_directories("${HIGGS_INSTALL_DIR}/lib")
```

第一句解决编译期头文件搜索，使源码能够使用：

```cpp
#include "interface.h"
#include "interface_loader.h"
```

第二句解决链接期库搜索。例如 CMake 中的 `HiggsOps` 通常会变成链接参数
`-lHiggsOps`，链接器再到 `install/lib` 查找 `libHiggsOps.so` 或
`libHiggsOps.a`。

#### 12.1.5 外部库列表

```cmake
set(INTERFACING_HIGGS_LIBRARIES
    HiggsOps HiggsIS spdlog deflate zmq pgm-5.2
    Threads::Threads dl)
```

这只是定义一个可复用列表。各项大致负责：

| 库 | 作用 |
| --- | --- |
| `HiggsOps` | 配置解析和工厂能力 |
| `HiggsIS` | 类加载、接口基类和异常 |
| `spdlog` | 日志 |
| `deflate` | 压缩相关依赖 |
| `zmq`、`pgm-5.2` | ZeroMQ 通信及传输依赖 |
| `Threads::Threads` | CMake 找到的平台线程库，Linux 通常对应 pthread |
| `dl` | Linux 的动态加载支持 |

不能只链接 `HiggsOps`，因为它自身还有一组传递依赖。当前内部包没有以现代 CMake
目标自动传播完整依赖，所以工程显式列出了经过验证的依赖闭包。

#### 12.1.6 loader 为什么是静态库，依赖为什么是 PUBLIC

```cmake
add_library(interfacing_loader STATIC interface_loader.cpp)
target_link_libraries(interfacing_loader
    PUBLIC ${INTERFACING_HIGGS_LIBRARIES})
```

`STATIC` 产生 `libinterfacing_loader.a`。它会被放进主程序，而不是作为运行时插件
加载。这里的 `PUBLIC` 同时表达两件事：

1. loader 自己需要这些 Higgs 库；
2. 链接 loader 的 `main.out` 和测试程序也必须继承这些库。

三种可见性的区别：

| 关键字 | 当前目标使用 | 下游目标继承 |
| --- | --- | --- |
| `PRIVATE` | 是 | 否 |
| `PUBLIC` | 是 | 是 |
| `INTERFACE` | 否 | 是 |

静态库只是目标文件归档，本身没有最终链接步骤。如果把这里错误地改成 `PRIVATE`，
最终链接 `main.out` 时可能出现 `undefined reference`。

#### 12.1.7 `add_subdirectory()` 建立整张构建图

```cmake
add_subdirectory(impl_a)
add_subdirectory(impl_b)
add_subdirectory(autotest)
```

CMake 会继续读取各子目录的 `CMakeLists.txt`。根目录先定义的普通变量和目标对子
目录可见，因此子目录可以引用 `${INTERFACING_HIGGS_LIBRARIES}` 和
`interfacing_loader`。正常构建默认目标时，A、B、main 都会被构建。

#### 12.1.8 为什么需要 RPATH

链接成功只说明链接器在**构建时**找到了库；程序启动时，Linux 动态加载器还必须
重新找到这些 `.so`。

```cmake
set(CMAKE_BUILD_RPATH "${HIGGS_INSTALL_DIR}/lib")
```

把源码工程的 `install/lib` 写入构建产物的运行时搜索路径，使 build 目录中的程序
无需修改系统 `/etc/ld.so.conf`，也通常不需要手动设置 `LD_LIBRARY_PATH`。

```cmake
add_link_options("-Wl,-rpath-link,${HIGGS_INSTALL_DIR}/lib")
```

`-Wl,` 表示把参数转交给 Linux 链接器；`-rpath-link` 帮助链接器在构建时寻找
`libHiggsOps.so` 自身依赖的其他共享库。

```cmake
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
```

`$ORIGIN` 表示可执行文件或共享库自身所在目录。若安装后主程序位于
`build/install/bin/main.out`，它会到 `build/install/lib` 找共享库。

注意：当前安装规则没有把全部 Higgs 第三方 `.so` 和 YAML 一并复制到
`build/install`。因此当前阶段保证的是“在源码的 build 目录运行”，并不代表已经
生成一个完全自包含、可以随意搬运的发布包。

### 12.2 impl_a/CMakeLists.txt

```cmake
add_library(impl_a SHARED impl_a.cpp)
target_include_directories(impl_a PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(impl_a PRIVATE ${INTERFACING_HIGGS_LIBRARIES})
set_target_properties(impl_a PROPERTIES
    OUTPUT_NAME "impl_a"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/lib")
install(TARGETS impl_a LIBRARY DESTINATION lib)
```

`impl_b/CMakeLists.txt` 完全相同，将目标名和文件名改成 `impl_b`。

显式设置各构建类型输出目录，是为了避免公共 CMake 宏的全局输出设置把文件放到
上一级目录，导致生成的配置路径难以理解。

### 12.3 autotest/CMakeLists.txt

```cmake
add_executable(main.out main.cpp)
target_link_libraries(main.out PRIVATE interfacing_loader)
set_target_properties(main.out PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin")
install(TARGETS main.out RUNTIME DESTINATION bin)
```

### 12.4 为什么 YAML 由 CMake 生成

配置中的 `class.file` 最终需要真实动态库路径。使用：

```cmake
$<TARGET_FILE:impl_a>
```

CMake 会根据实际构建目录和配置替换路径，避免手写 `Debug/Release` 或猜测输出目录。

生成的是绝对路径。因此工程复制到另一台服务器后必须重新运行 CMake configure，
不能直接复制旧 YAML。

`${PROJECT_VERSION}` 是普通 CMake 变量，在配置阶段展开；
`$<TARGET_FILE:impl_a>` 是生成器表达式，在生成阶段根据真实目标展开。例如服务器
上最终 YAML 可能是：

```yaml
class:
  file: /home/jinkaisheng/Interfacing/build/server/lib/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-A
```

### 12.5 `install()` 不等于构建

```cmake
install(TARGETS impl_a LIBRARY DESTINATION lib)
install(TARGETS main.out RUNTIME DESTINATION bin)
```

这些语句不会改变普通的：

```bash
cmake --build build/server
```

它们只在执行以下命令时生效：

```bash
cmake --install build/server
```

CMake 中常见产物分类如下：

| 产物 | CMake 分类 | 本项目示例 |
| --- | --- | --- |
| 可执行程序 | `RUNTIME` | `main.out` |
| 共享库 | `LIBRARY` | `libimpl_a.so` |
| 静态库 | `ARCHIVE` | `libinterfacing_loader.a` |

### 12.6 在服务器上手动完成本阶段

先确认第 7 阶段的依赖恢复结果存在：

```bash
cd ~/Interfacing
test -f install/include/higgsops/ConfigFactory.h
test -f install/include/higgsIS/ClassLoader.h
test -f install/lib/libHiggsOps.so
test -f install/lib/libHiggsIS.so
```

第一次实现动态加载且尚未创建 `tests/` 时：

```bash
cmake -S . -B build/server -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=OFF
cmake --build build/server -j 4
```

完成测试代码后改成：

```bash
cmake -S . -B build/server -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=ON
cmake --build build/server -j 4
ctest --test-dir build/server --output-on-failure
```

检查关键产物：

```bash
find build/server -maxdepth 3 -type f \
  \( -name 'main.out' -o -name '*.so' -o -name '*.yaml' \)
cat build/server/config/impl_a.yaml
cat build/server/config/impl_b.yaml
```

需要观察 CMake 实际传给编译器和链接器的命令时使用：

```bash
cmake --build build/server --verbose
```

### 12.7 四类常见错误及定位方式

#### A. 找不到头文件

```text
fatal error: higgsops/ConfigFactory.h: No such file or directory
```

这是编译期头文件搜索问题：

```bash
ls install/include/higgsops/ConfigFactory.h
grep HIGGS_INSTALL_DIR build/server/CMakeCache.txt
```

#### B. 找不到 `-lHiggsOps`

```text
cannot find -lHiggsOps
```

这是链接期库搜索问题：

```bash
ls -l install/lib/libHiggsOps*
cmake --build build/server --verbose
```

#### C. `undefined reference`

头文件已经找到并完成编译，但最终链接时可能缺少库、库顺序不正确，或二进制 ABI
不匹配。查看详细链接命令：

```bash
cmake --build build/server --verbose
```

#### D. 程序启动时报缺少 `.so`

```text
error while loading shared libraries: libHiggsIS.so: cannot open shared object file
```

这是运行期动态库搜索问题：

```bash
ldd build/server/bin/main.out
ldd build/server/lib/libimpl_a.so
readelf -d build/server/bin/main.out | grep -E 'RPATH|RUNPATH'
```

仅用于诊断时可临时执行：

```bash
LD_LIBRARY_PATH="$PWD/install/lib" \
  ./build/server/bin/main.out build/server/config/impl_a.yaml
```

如果加上 `LD_LIBRARY_PATH` 后可以运行，问题基本就在 RPATH 或动态库目录。

### 12.8 本阶段验收问题

完成本阶段后，应能独立回答：

1. `install/` 由谁生成？——`configuration.py`，不是 CMake。
2. `build/server/` 由谁生成？——CMake 和 Make/Ninja。
3. 谁把 `.cpp` 变成 `.so`？——编译器和链接器，CMake 只生成规则。
4. 为什么 `impl_a` 是 `SHARED`？——它需要在运行时动态加载。
5. 为什么 loader 的依赖是 `PUBLIC`？——主程序需要继承静态 loader 的链接依赖。
6. 为什么 YAML 由 CMake 生成？——只有 CMake 知道 `.so` 的真实构建路径。
7. 为什么需要 RPATH？——链接时找到 `.so` 不代表运行时仍能找到。

---

## 13. 第 10 阶段：第一次进阶编译与符号验证

```bash
cmake -S . -B build/server -G Ninja
cmake --build build/server -j 4
```

此阶段 `INTERFACING_BUILD_TESTS` 暂时为 OFF，因为你还没有创建 `tests/`。这样可以
先独立验证动态加载机制。完成第 14 节时，把根 CMake 中该 option 的默认值改成
`ON`，并显式使用 `-DINTERFACING_BUILD_TESTS=ON` 重新配置。

先不要急着运行 main，检查配置：

```bash
cat build/server/config/impl_a.yaml
cat build/server/config/impl_b.yaml
```

正确结构必须是：

```yaml
class:
  file: /绝对路径/libimpl_a.so
  ver: 1.0.0
  class: ImplA
message: configured-A
```

注意最后一个键是 `class`，不是旧注释里的 `name`。

检查动态符号：

```bash
nm -D --defined-only build/server/lib/libimpl_a.so \
  | c++filt | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

必须同时看到：

```text
HCL_DynamicLibVersion
ImplA::NewInstance(char const*)
```

只看到版本符号而看不到 `NewInstance` 时，首先检查它是不是错误地直接定义在类体内。

检查运行库：

```bash
ldd build/server/bin/main.out | grep 'not found' || true
ldd build/server/lib/libimpl_a.so | grep 'not found' || true
```

预期不出现 `not found`。

运行：

```bash
build/server/bin/main.out build/server/config/impl_a.yaml
build/server/bin/main.out build/server/config/impl_b.yaml
```

预期分别包含：

```text
Implementation A [configured-A]
Implementation B [configured-B]
```

这证明业务配置通过 token 进入了对应插件，而不只是加载了不同 `.so`。

### 建议提交一次

```bash
git add CMakeLists.txt interface.h interface_loader.* \
  impl_a impl_b autotest
git commit -m "feat: load implementations through HiggsOps configuration"
```

---

## 14. 第 11 阶段：添加版本错误测试

### 14.1 为什么需要一个 LegacyImpl

直接构造“虚函数表已经完全不兼容”的旧插件可能触发未定义行为，不适合稳定单元测试。
因此创建一个布局仍安全、但主动报告 `0.9.0` 的测试插件，用它证明版本校验的效果。

新建 `tests/legacy_impl.cpp`：

```cpp
#include "interface.h"
#include <higgsops/ConfigFactory.h>
#include <iostream>

class LegacyImpl final : public Interface {
public:
    void print() override {
        std::cout << "Legacy implementation - print()" << std::endl;
    }
    void foo() override {
        std::cout << "Legacy implementation - foo()" << std::endl;
    }
    void bar() override {
        std::cout << "Legacy implementation has old bar() semantics"
                  << std::endl;
    }
    std::string GetVersion() override {
        return "0.9.0";
    }

    static HiggsIS::Loadable* NewInstance(const char* config_token);
};

HiggsIS::Loadable* LegacyImpl::NewInstance(const char* config_token) {
    (void)higgsops::GetAssignedConfig(config_token);
    return new LegacyImpl();
}

HCL_SO_VERSION("0.9.0")
```

### 14.2 测试至少覆盖六项

在 `tests/interface_tests.cpp` 中实现：

```text
VersionCompatibility.FollowsSemanticVersionRules
DynamicLoading.LoadsImplementationAFromConfiguration
DynamicLoading.LoadsImplementationBByChangingOnlyConfiguration
VersionValidation.RejectsIncompatibleImplementation
VersionValidation.DisabledValidationLetsMismatchEscape
ClassLoaderValidation.RejectsWrongDeclaredLibraryVersion
```

完整的 `tests/interface_tests.cpp`：

```cpp
#include "interface_loader.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

std::string ConfigPath(const char* directory, const char* file) {
    return std::string(directory) + "/" + file;
}

TEST(VersionCompatibility, FollowsSemanticVersionRules) {
    EXPECT_TRUE(IsInterfaceVersionCompatible("1.0.0", "1.0.0"));
    EXPECT_TRUE(IsInterfaceVersionCompatible("1.0.0", "1.2.0"));
    EXPECT_FALSE(IsInterfaceVersionCompatible("1.1.0", "1.0.9"));
    EXPECT_FALSE(IsInterfaceVersionCompatible("1.0.0", "2.0.0"));
    EXPECT_THROW(IsInterfaceVersionCompatible("1.0", "1.0.0"),
                 std::invalid_argument);
}

TEST(DynamicLoading, LoadsImplementationAFromConfiguration) {
    LoadedInterface loaded = LoadInterfaceWithModeFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_a.yaml"));

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Dynamic);
    EXPECT_EQ(loaded.instance->GetVersion(), INTERFACE_VERSION);
    testing::internal::CaptureStdout();
    loaded.instance->print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("configured-A"),
              std::string::npos);
}

TEST(DynamicLoading, LoadsImplementationBByChangingOnlyConfiguration) {
    LoadedInterface loaded = LoadInterfaceWithModeFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_b.yaml"));

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Dynamic);
    testing::internal::CaptureStdout();
    loaded.instance->print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("configured-B"),
              std::string::npos);
}

TEST(VersionValidation, RejectsIncompatibleImplementation) {
    EXPECT_THROW(
        LoadInterfaceWithModeFromConfig(
            ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml")),
        std::runtime_error);
}

TEST(VersionValidation, DisabledValidationLetsMismatchEscape) {
    LoadedInterface loaded = LoadInterfaceWithModeFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml"), false);

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Dynamic);
    EXPECT_EQ(loaded.instance->GetVersion(), "0.9.0");
    EXPECT_FALSE(IsInterfaceVersionCompatible(INTERFACE_VERSION,
                                              loaded.instance->GetVersion()));
}

TEST(ClassLoaderValidation, RejectsWrongDeclaredLibraryVersion) {
    EXPECT_ANY_THROW(LoadInterfaceWithModeFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR,
                   "wrong-declared-version.yaml"),
        false));
}

}  // namespace
```

这两个版本测试关注不同层：

```text
HCL_SO_VERSION:
配置宣称的插件版本 vs 文件自身声明的插件版本

GetVersion:
插件编译时 Interface 版本 vs 主程序需要的 Interface 版本
```

### 14.3 完整的 tests/CMakeLists.txt

```cmake
add_library(legacy_impl SHARED legacy_impl.cpp)
target_include_directories(legacy_impl PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(legacy_impl PRIVATE ${INTERFACING_HIGGS_LIBRARIES})
set_target_properties(legacy_impl PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/lib")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-config")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test-config/legacy.yaml"
    CONTENT
"class:\n  file: $<TARGET_FILE:legacy_impl>\n  ver: 0.9.0\n  class: LegacyImpl\nmessage: legacy\n")

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/test-config/wrong-declared-version.yaml"
    CONTENT
"class:\n  file: $<TARGET_FILE:impl_a>\n  ver: 9.9.9\n  class: ImplA\nmessage: wrong-version\n")

add_executable(interfacing_tests interface_tests.cpp)
target_link_libraries(interfacing_tests PRIVATE
    interfacing_loader gtest gtest_main)
target_compile_definitions(interfacing_tests PRIVATE
    INTERFACING_CONFIG_DIR="${CMAKE_BINARY_DIR}/config"
    INTERFACING_TEST_CONFIG_DIR="${CMAKE_BINARY_DIR}/test-config")
add_dependencies(interfacing_tests impl_a impl_b legacy_impl)
set_target_properties(interfacing_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin")

include(GoogleTest)
gtest_discover_tests(interfacing_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=${HIGGS_INSTALL_DIR}/lib")
```

使用 `PRE_TEST` 的原因：gtest 测试发现也需要启动测试二进制；延迟到 ctest 阶段并
带上 `LD_LIBRARY_PATH`，能避免构建阶段因间接动态库尚未解析而失败。

### 14.4 运行测试

先把根 `CMakeLists.txt` 中：

```cmake
option(INTERFACING_BUILD_TESTS "Build Interfacing tests" OFF)
```

改为最终状态：

```cmake
option(INTERFACING_BUILD_TESTS "Build Interfacing tests" ON)
```

然后显式刷新已有 CMake 缓存：

```bash
cmake -S . -B build/server -G Ninja -DINTERFACING_BUILD_TESTS=ON
cmake --build build/server -j 4
ctest --test-dir build/server --output-on-failure
```

验收：

```text
100% tests passed, 0 tests failed out of 6
```

建议提交：

```bash
git add tests CMakeLists.txt
git commit -m "test: cover dynamic loading and version validation"
```

---

## 15. 第 12 阶段：增加一键脚本和 VS Code 任务

### 15.1 compile.sh

创建完整脚本：

```bash
#!/usr/bin/env bash

set -euo pipefail

action="${1:-all}"
build_dir="build/server"

install_dependencies() {
    python py/configuration.py dependency.nuspec.in install/
}

build_project() {
    cmake -S . -B "${build_dir}" -G Ninja
    cmake --build "${build_dir}" -j "${BUILD_JOBS:-4}"
}

test_project() {
    ctest --test-dir "${build_dir}" --output-on-failure
}

case "${action}" in
    deps)
        install_dependencies
        ;;
    build)
        build_project
        ;;
    test)
        test_project
        ;;
    all)
        install_dependencies
        build_project
        test_project
        ;;
    *)
        echo "Usage: bash compile.sh {deps|build|test|all}" >&2
        exit 2
        ;;
esac
```

日常完整验证：

```bash
bash compile.sh all
```

原因：把团队约定的命令固化，减少每个人手工使用不同构建目录或漏掉测试。

### 15.2 VS Code tasks

创建 `.vscode/tasks.json`：

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Interfacing: dependencies",
            "type": "shell",
            "command": "bash compile.sh deps",
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": []
        },
        {
            "label": "Interfacing: build",
            "type": "shell",
            "command": "bash compile.sh build",
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "Interfacing: test",
            "type": "shell",
            "command": "bash compile.sh test",
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": []
        },
        {
            "label": "Interfacing: all",
            "type": "shell",
            "command": "bash compile.sh all",
            "options": { "cwd": "${workspaceFolder}" },
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "Interfacing: run A",
            "type": "shell",
            "command": "build/server/bin/main.out build/server/config/impl_a.yaml",
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": []
        },
        {
            "label": "Interfacing: run B",
            "type": "shell",
            "command": "build/server/bin/main.out build/server/config/impl_b.yaml",
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": []
        }
    ]
}
```

Remote-SSH 打开服务器目录后，通过 `Terminal -> Run Task` 执行。任务运行在远端，
因此不会发生 Windows 尝试执行 Linux `.so` 的问题。

建议提交：

```bash
git add compile.sh .vscode README.md
git commit -m "docs: add reproducible build and VS Code workflow"
```

---

## 16. 完整的从零验收

完成所有代码后，模拟一个新用户验证。不要使用当前 `install/` 和 `build/`。

最安全的方式不是在原目录删除，而是重新克隆到另一个目录：

```bash
cd ~
git clone --branch feature/higgsops-advanced --recurse-submodules \
  https://github.com/JinKaisheng/Interfacing.git \
  Interfacing-acceptance
cd Interfacing-acceptance

python py/configuration.py dependency.nuspec.in install/
cmake -S . -B build/server -G Ninja
cmake --build build/server -j 4
ctest --test-dir build/server --output-on-failure

build/server/bin/main.out build/server/config/impl_a.yaml
build/server/bin/main.out build/server/config/impl_b.yaml
```

注意：只有把 feature 分支推送到 GitHub 后，上述新克隆才能 checkout 它。

推送：

```bash
git status
git log --oneline --decorate -8
git push -u origin feature/higgsops-advanced
```

然后在 GitHub 创建 PR，将 feature 分支合并到 `main`。合并后再做一次只依赖
`main` 的干净克隆验收。

---

## 17. 常见失败与定位方法

### 17.1 SSH 可以登录但 VS Code 连接失败

先确认本机是 VS Code 1.98.2、Remote-SSH 0.118.0。CentOS 7 无法运行新版
VS Code Server。

### 17.2 configuration.py 下载失败

依次检查：

```bash
nuget sources List
curl -I https://nexusrm.higgsasset.com
python -c 'import termcolor; print("ok")'
```

可能原因：公司网络、Nexus 认证、NuGet 源、Python 模块。

### 17.3 找不到 ConfigFactory.h

```bash
ls install/include/higgsops/ConfigFactory.h
```

文件不存在说明依赖没有成功恢复；文件存在则检查 CMake 是否正确设置
`HIGGS_INSTALL_DIR` 并 include `higgs_common`。

### 17.4 cannot find -lHiggsOps

```bash
ls -l install/lib/libHiggsOps.so
```

存在则检查 `link_directories`；不存在则重新检查依赖脚本的 OS 判断和安装日志。

### 17.5 ClassNotFoundException

按顺序检查：

```bash
cat build/server/config/impl_a.yaml
ls -l <class.file 的实际路径>
nm -D --defined-only <class.file> | c++filt
```

核对：

- 文件存在；
- `class.ver` 等于 `HCL_SO_VERSION`；
- `class.class` 是完整类名；
- 存在 `NewInstance(char const*)` 动态符号；
- YAML 使用 `class` 而不是 `name`。

### 17.6 主程序启动时报库 not found

```bash
ldd build/server/bin/main.out
ldd build/server/lib/libimpl_a.so
readelf -d build/server/bin/main.out | grep -E 'RPATH|RUNPATH'
```

不要把运行时库缺失误判成 C++ 代码错误。

### 17.7 换目录后 YAML 仍指向旧路径

重新运行：

```bash
cmake -S . -B build/server -G Ninja
```

原因：生成配置包含绝对动态库路径。

### 17.8 版本校验没有生效

确认业务代码调用的是：

```cpp
LoadInterfaceWithModeFromConfig(config)
```

而不是：

```cpp
higgsops::LoadClass<Interface>(...)
LoadInterfaceWithModeFromConfig(config, false)
```

统一入口的价值就是让默认路径永远执行版本检查。

---

## 18. 新增 ImplC 时应该怎样做

完成进阶后，新增实现只需要：

1. 新建 `impl_c/impl_c.cpp`；
2. 继承 `Interface`；
3. 实现全部虚函数和 `GetVersion()`；
4. 类内声明、类外定义 `ImplC::NewInstance`；
5. 添加 `HCL_SO_VERSION(INTERFACE_VERSION)`；
6. CMake 添加 `impl_c` 动态库目标；
7. 生成 `impl_c.yaml`；
8. 使用同一个 `main.out impl_c.yaml`。

如果第 8 步需要修改 `main.cpp`，说明选择实现的逻辑没有真正配置化。

有命名空间时，YAML 必须写完整名称：

```yaml
class:
  file: /path/to/libimpl_c.so
  ver: 1.0.0
  class: my_namespace::ImplC
```

---

## 19. 最终自检清单

### Git 与工程结构

- [ ] 从基础提交 `13b51cb` 建立了 feature 分支；
- [ ] `py` 是 submodule，不是无来源复制目录；
- [ ] `build/`、`install/` 没有提交；
- [ ] 每个阶段有可理解的 Git 提交。

### 依赖

- [ ] `dependency.nuspec.in` 存在；
- [ ] 从空 `install/` 能恢复全部依赖；
- [ ] `used_package.json` 版本符合预期；
- [ ] CentOS/Rocky 使用了正确库目录。

### Interface 与插件

- [ ] `Interface` 继承 `HiggsIS::Loadable`；
- [ ] 有虚析构；
- [ ] 有 `INTERFACE_VERSION` 与 `GetVersion()`；
- [ ] A/B 都类外定义 `NewInstance`；
- [ ] A/B 都有 `HCL_SO_VERSION`；
- [ ] `nm -D` 能看到两个关键符号。

### 解耦

- [ ] `main.cpp` 没有 `dlopen`/`dlsym`；
- [ ] `main.cpp` 不包含任何具体实现；
- [ ] 修改 YAML 即可切换 A/B；
- [ ] 新增实现不需要修改 main。

### 版本与测试

- [ ] HCL 声明版本错误会失败；
- [ ] Interface 旧版本会失败；
- [ ] major 不同会失败；
- [ ] 关闭校验的风险有测试说明；
- [ ] 6 个测试全部通过。

### 运行

- [ ] `ldd` 没有 `not found`；
- [ ] A 输出 `configured-A`；
- [ ] B 输出 `configured-B`；
- [ ] 换目录后重新生成 YAML；
- [ ] 干净克隆可以重新完成依赖、构建和测试。

---

## 20. 推荐学习顺序

第一次不要直接复制最终代码。建议按以下节奏亲自操作：

```text
第一遍：
基础版运行 -> 看 dlopen/dlsym -> 看动态符号

第二遍：
只接依赖 -> 阅读 ClassLoader.h 和 ConfigFactory.h

第三遍：
只改一个 ImplA -> 检查 HCL/NewInstance 符号 -> 成功加载 A

第四遍：
复制协议实现 B -> 证明只改 YAML 即可切换

第五遍：
加入 Interface 版本规则和 LegacyImpl -> 观察校验开关差异

第六遍：
加入 gtest、compile.sh、VS Code tasks -> 做干净克隆验收
```

这样学习的重点不是“得到一份能跑的代码”，而是能明确判断一个错误发生在：

```text
依赖解析、编译、链接、运行库搜索、动态符号、类加载协议、Interface ABI、版本策略
```

当你能先判断层次，再选择 `configuration.py`、编译器错误、`ldd`、`nm`、YAML 或
版本测试中的正确工具时，就真正掌握了这套机制。
