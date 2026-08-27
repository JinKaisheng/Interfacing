# 开发命令与排障速查

本文集中记录 Interfacing、Shannon C++ 工程、服务器开发、Git、CMake、动态库和
VS Code 索引中常用的细节命令，便于按问题类型快速查找。

## 0. 先分清命令在哪执行

| 标记 | 执行位置 | 常见提示符 |
|---|---|---|
| Windows | 本机 PowerShell | `PS C:\...>` |
| Server | SSH 登录后的 Linux Shell | `[jinkaisheng@ShannonTest ...]$` |
| VS Code | 命令面板 | `Ctrl+Shift+P` |

基本原则：

```text
Windows 路径 C:\... 只能由本机 PowerShell 直接访问。
Linux 路径 /home/... 只能由服务器 Shell 直接访问。
scp 跨机器复制，cp 在服务器内部复制。
```

文中示例项目路径：

```text
Windows：C:\LocalCode\Interfacing
服务器：/home/jinkaisheng/InterfacingJin/Interfacing
算法库：/home/jinkaisheng/InterfacingJin/shannon_algo_db
```

## 1. Windows SSH Agent

### 1.1 查看服务状态

在 Windows PowerShell 执行：

```powershell
Get-Service ssh-agent
```

### 1.2 启动并设置自动启动

以下命令需要“以管理员身份运行”的 PowerShell：

```powershell
Set-Service -Name ssh-agent -StartupType Automatic
Start-Service ssh-agent
Get-Service ssh-agent
```

预期状态：

```text
Running  ssh-agent
```

检查当前 PowerShell 是否为管理员：

```powershell
([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)
```

### 1.3 检查和加载密钥

注意正确路径中 `jinka` 与 `.ssh` 之间有反斜杠：

```powershell
Test-Path "$env:USERPROFILE\.ssh\id_ed25519"
ssh-add "$env:USERPROFILE\.ssh\id_ed25519"
ssh-add -l
```

复制公钥：

```powershell
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub" | Set-Clipboard
```

不要显示、复制或上传没有 `.pub` 后缀的私钥。

查看公钥指纹：

```powershell
ssh-keygen -lf "$env:USERPROFILE\.ssh\id_ed25519.pub"
```

清空 Agent 中已加载的身份不会删除密钥文件：

```powershell
ssh-add -D
```

## 2. SSH 登录和 Agent Forwarding

### 2.1 Windows SSH 配置

文件位置：

```text
C:\Users\jinka\.ssh\config
```

建议配置：

```sshconfig
Host 10.214.2.51
    HostName 10.214.2.51
    User jinkaisheng
    Port 22
    IdentityFile C:/Users/jinka/.ssh/id_ed25519
    IdentitiesOnly yes
    ForwardAgent yes
```

### 2.2 连接服务器

Windows PowerShell：

```powershell
ssh -A jinkaisheng@10.214.2.51
```

如果 `ForwardAgent yes` 已生效，也可执行：

```powershell
ssh 10.214.2.51
```

### 2.3 在服务器验证转发

Server：

```bash
echo "$SSH_AUTH_SOCK"
ssh-add -l
ssh -T git@github.higgsasset.com
```

成功认证通常显示：

```text
Hi jinkaisheng! You've successfully authenticated,
but GitHub does not provide shell access.
```

这条提示表示认证成功；GitHub 不提供普通 Shell 是正常行为。

### 2.4 关闭 SSH 连接

Server：

```bash
exit
```

也可以按 `Ctrl+D`。连接卡死时，先按 Enter，再输入：

```text
~.
```

## 3. Git 仓库操作

### 3.1 使用 SSH 克隆公司仓库

Server：

```bash
git clone git@github.higgsasset.com:Shannon/shannon_algo_db.git
```

终端中只粘贴纯 URL，不要粘贴 Markdown 形式：

```text
[地址](地址)    错误
git@主机:组织/仓库.git    正确
```

克隆前验证读取权限：

```bash
git ls-remote git@github.higgsasset.com:Shannon/shannon_algo_db.git HEAD
```

### 3.2 查看仓库状态

```bash
git status --short --branch
git remote -v
git log -1 --oneline
```

### 3.3 配置提交身份

只设置当前仓库：

```bash
git config --local user.name "JinKaisheng"
git config --local user.email "你的GitHub登记邮箱"
```

检查：

```bash
git config --local --get user.name
git config --local --get user.email
```

### 3.4 检查并提交变更

```bash
git status --short
git diff
git diff --cached
git add README.md
git diff --cached -- README.md
git commit -m "docs: update README"
```

不要在未确认变更范围时使用：

```text
git reset --hard
git clean -fd
```

它们可能不可恢复地删除本地代码。

## 4. Git 子模块

### 4.1 新增子模块

只有主仓库尚未登记该子模块时才执行：

```bash
git submodule add \
  git@github.higgsasset.com:Shannon/shannon_py_cmake.git py
```

检查并提交：

```bash
git submodule status
git diff --cached --submodule=log
git add .gitmodules py
git commit -m "build: add shannon_py_cmake submodule"
```

### 4.2 初始化仓库已有子模块

如果提示：

```text
'py' already exists in the index
```

说明它已经是子模块，不要再次 `submodule add`。执行：

```bash
git submodule update --init --recursive
```

### 4.3 读取子模块状态

```bash
git submodule status
```

首字符含义：

```text
-<commit> py   尚未初始化
 <commit> py   已初始化且提交正确
+<commit> py   当前提交与主仓库记录不一致
U<commit> py   存在合并冲突
```

### 4.4 `.gitmodules` 是 HTTPS，但服务器只能用 SSH

检查默认 URL：

```bash
git config -f .gitmodules --get submodule.py.url
```

只为当前工作副本覆盖成 SSH，不修改 `.gitmodules`：

```bash
git config --local submodule.py.url \
  git@github.higgsasset.com:Shannon/shannon_py_cmake.git
```

然后执行：

```bash
git config --local --get submodule.py.url
git submodule update --init --recursive
git -C py remote -v
```

设置本地覆盖后不要立即执行 `git submodule sync`，否则可能重新使用
`.gitmodules` 中的 HTTPS 地址。

## 5. Windows 与服务器文件传输

### 5.1 Windows 上传单个文件

在 Windows PowerShell 执行：

```powershell
scp "C:\LocalCode\Interfacing\README.md" `
  jinkaisheng@10.214.2.51:/home/jinkaisheng/InterfacingJin/Interfacing/
```

### 5.2 Windows 上传目录

```powershell
scp -r "C:\LocalCode\Interfacing\impl_a" `
  jinkaisheng@10.214.2.51:/home/jinkaisheng/InterfacingJin/Interfacing/
```

### 5.3 从服务器下载文件

Windows PowerShell：

```powershell
scp jinkaisheng@10.214.2.51:/home/jinkaisheng/InterfacingJin/Interfacing/README.md `
  "C:\LocalCode\"
```

下载整个目录：

```powershell
scp -r jinkaisheng@10.214.2.51:/home/jinkaisheng/InterfacingJin/Interfacing `
  "C:\LocalCode\ServerBackup\"
```

记忆规则：

```text
scp 来源 目标
```

### 5.4 服务器内部复制

Server：

```bash
cp -i /path/source.txt /path/destination/
cp -r /path/source_directory /path/destination/
cp -a /path/source_directory/. /path/destination_directory/
```

`-i` 在覆盖前询问；`-a` 尽量保留目录结构、权限和时间。

## 6. 恢复 C++ 工程依赖

### 6.1 初始化公共 CMake 子模块

```bash
git submodule update --init --recursive
test -f py/configuration.py
```

### 6.2 恢复 NuGet 依赖闭包

在项目根目录执行：

```bash
python py/configuration.py dependency.nuspec.in install/
```

检查关键文件：

```bash
test -f install/include/higgsops/ConfigFactory.h
test -f install/include/higgsops/LoggerFactory.h
test -f install/include/higgsIS/ClassLoader.h
test -f install/lib/libHiggsOps.so
```

只复制一个 HiggsOps `.so` 不够；NuGet 脚本负责恢复 HiggsIS、spdlog、ZeroMQ、
libdeflate 等传递依赖。

## 7. CMake、Build、Test、Install

### 7.1 配置工程

Debug 用于日常调试：

```bash
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINTERFACING_BUILD_TESTS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

advanced Release 用于最终验收：

```bash
cmake -S . -B build/advanced -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DINTERFACING_BUILD_TESTS=ON
```

参数含义：

```text
-S .                              源码目录
-B build/debug                    Debug 构建目录
-B build/advanced                 Release 验收构建目录
-G Ninja                          使用 Ninja
CMAKE_BUILD_TYPE                  选择 Debug 或 Release
INTERFACING_BUILD_TESTS=ON         构建测试
CMAKE_EXPORT_COMPILE_COMMANDS=ON  生成 C++ 索引数据库
```

### 7.2 构建

```bash
cmake --build build/debug -j 4
cmake --build build/advanced -j 4
```

如果输出：

```text
ninja: no work to do.
```

表示目标已经是最新状态，不是错误。

强制清理后重建：

```bash
cmake --build build/debug --clean-first -j 4
```

仅在确实需要完整重编译时使用。

### 7.3 运行测试

```bash
ctest --test-dir build/debug --output-on-failure
ctest --test-dir build/advanced --output-on-failure
```

显示详细测试过程：

```bash
ctest --test-dir build/debug --output-on-failure -VV
```

### 7.4 安装

```bash
cmake --install build/advanced
```

区别：

```text
build   编译和链接，产物位于构建目录
install 按 install(...) 规则复制、整理已有构建产物
```

### 7.5 查看 CMake 安装前缀

```bash
cmake -LA -N build/advanced | grep CMAKE_INSTALL_PREFIX
```

## 8. 运行 Interfacing

```bash
build/debug/bin/main.out build/debug/config/impl_a_static.yaml
build/debug/bin/main.out build/debug/config/impl_a_dynamic.yaml
```

检查生成文件：

```bash
find build/debug -maxdepth 3 -type f \
  \( -name '*.so' -o -name 'main.out' -o -name '*.yaml' \) \
  -print
```

## 9. 动态库排障

### 9.1 检查导出符号

```bash
nm -D --defined-only build/debug/lib/libimpl_ad.so \
  | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

预期包含：

```text
ImplA::NewInstance(char const*)
HCL_DynamicLibVersion
```

### 9.2 检查动态依赖

```bash
ldd build/debug/bin/main.out
ldd build/debug/lib/libimpl_ad.so
```

重点查找：

```text
not found
```

### 9.3 检查 RPATH

```bash
readelf -d build/debug/bin/main.out | grep -E 'RPATH|RUNPATH'
readelf -d build/debug/lib/libimpl_ad.so | grep -E 'RPATH|RUNPATH'
```

### 9.4 检查 YAML

```bash
sed -n '1,120p' build/debug/config/impl_a_dynamic.yaml
```

需要满足：

```text
class.file   指向真实存在的 .so
class.ver    等于 .so 的 HCL_SO_VERSION
class.class  等于完整 C++ 类名
```

## 10. 查找 C++ 声明和定义

### 10.1 使用 ripgrep

```bash
rg -n "GetLogger" . install py
rg -n "LoadConfigFile|LoadClass" install/include
rg -n "class Map" install/include/higgsops/ConfigFactory.h
```

如果服务器没有 `rg`：

```bash
grep -RIn "GetLogger" . install py
```

### 10.2 按文件名查找

```bash
find . -type f -name 'LoggerFactory.h' -print
find . -type f -name 'ConfigFactory.h' -print
find . -type f -name 'ClassLoader.h' -print
```

### 10.3 带行号查看文件片段

```bash
nl -ba install/include/higgsops/LoggerFactory.h | sed -n '1,100p'
```

### 10.4 `GetLogger<T>()` 的位置与调用链

定义位于：

```text
install/include/higgsops/LoggerFactory.h
```

调用：

```cpp
higgsops::GetLogger<AlgoBusDbApiHolder>()
```

展开逻辑：

```text
GetLogger<T>()
  -> GetLogger(typeid(T))
  -> GetLogger(HiggsIS::GetClassName(clazz))
  -> GetLogger(const std::string& loggerId)
```

模板和 inline 重载在头文件中；最底层字符串重载的实现位于
`libHiggsOps.so`。如果依赖包未提供 `.cpp`，VS Code 只能跳到声明，不能跳到
二进制库内部源码。

## 11. VS Code 无法跳转到定义

### 11.1 先确认依赖头文件存在

```bash
ls -l install/include/higgsops/LoggerFactory.h
```

如果不存在，先恢复依赖：

```bash
python py/configuration.py dependency.nuspec.in install/
```

### 11.2 生成编译数据库

```bash
cmake -S . -B build/static -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

检查：

```bash
ls -l build/static/compile_commands.json
```

### 11.3 配置 VS Code C/C++ 扩展

`.vscode/settings.json`：

```json
{
    "C_Cpp.default.compileCommands": "${workspaceFolder}/build/static/compile_commands.json"
}
```

然后在 VS Code 命令面板执行：

```text
C/C++: Reset IntelliSense Database
Developer: Reload Window
```

### 11.4 理解“只能跳到声明”

以下情况不是索引故障：

```text
头文件只有函数声明
真实实现已经编译进第三方 .so
依赖包没有提供实现源码
```

此时 F12 能跳到头文件声明，但无法跳到第三方库内部 `.cpp`。

## 12. VS Code 标签页只能显示一个

临时固定预览标签：双击文件，或按 `Ctrl+K` 后按 Enter。

用户设置建议：

```json
{
    "workbench.editor.enablePreview": false,
    "workbench.editor.enablePreviewFromQuickOpen": false,
    "workbench.editor.showTabs": "multiple",
    "workbench.editor.limit.enabled": false
}
```

Remote SSH 窗口还需要检查：

```text
Preferences: Open Remote Settings (JSON)
```

项目级设置位于：

```text
.vscode/settings.json
```

修改后执行：

```text
Developer: Reload Window
```

## 13. 高频错误对照表

| 错误 | 常见原因 | 首选检查 |
|---|---|---|
| `Permission denied (publickey)` | Agent 未加载或未转发 | `ssh-add -l`、`SSH_AUTH_SOCK` |
| HTTPS 返回 403 | 禁用密码认证 | 改用 SSH URL |
| `py already exists in the index` | 子模块已登记 | `git submodule update --init --recursive` |
| 子模块仍走 HTTPS | `.gitmodules` 使用 HTTPS | 设置本地 `submodule.py.url` |
| `Author identity unknown` | Git 作者未配置 | `git config --local user.*` |
| 头文件找不到 | 文件缺失或 include 路径错误 | `find`、编译命令中的 `-I` |
| `undefined reference` | 链接库缺失 | `target_link_libraries`、依赖闭包 |
| `.so: cannot open shared object` | 运行时搜索路径错误 | `ldd`、RPATH |
| ClassLoader 找不到类 | 类名或工厂符号错误 | YAML、`nm -D` |
| YAML 版本不匹配 | `class.ver` 与 HCL 版本不同 | YAML、`HCL_SO_VERSION` |
| F12 无法跳转 | 依赖或编译数据库缺失 | `install/include`、`compile_commands.json` |
| `ninja: no work to do` | 构建已经最新 | 正常，无需处理 |

## 14. 最短验收命令序列

```bash
git submodule update --init --recursive
bash compile.sh deps
bash .agents/skills/interfacing-maintenance/scripts/verify.sh

nm -D --defined-only build/debug/lib/libimpl_ad.so \
  | c++filt \
  | grep -E 'NewInstance|HCL_DynamicLibVersion'
```

这组命令依次验证：子模块、依赖、Debug/Release 编译、全部测试、静态/动态主程序
和插件导出协议。依赖已恢复时可以跳过 `bash compile.sh deps`。
