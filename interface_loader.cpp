#include "interface_loader.h"

#include "builtin_interfaces.h"

#include <higgsops/ConfigFactory.h>

#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>

// 版本兼容性校验规则：
//
// 1. 版本格式必须为 MAJOR.MINOR.PATCH，例如 1.2.3。
// 2. 主程序版本表示其要求实现提供的最低接口版本。
// 3. 实现版本的 MAJOR 必须与主程序要求的 MAJOR 相同。
//    MAJOR 不同表示接口可能存在破坏性变化，因此拒绝加载。
// 4. 在 MAJOR 相同的情况下，实现版本必须大于或等于主程序要求的版本。
//    比较顺序依次为 MAJOR、MINOR、PATCH，并按整数进行比较。
// 5. 允许加载同一 MAJOR 下更新的 MINOR 或 PATCH 版本，
//    因为约定同一 MAJOR 内的新版本保持向后兼容。
// 6. 拒绝低于主程序最低要求的旧实现，因为它可能缺少所需功能或修复。
// 7. 此规则依赖团队遵守语义化版本约定：
//    兼容更新只增加 MINOR/PATCH，破坏兼容性的修改必须增加 MAJOR。
//
// 示例：主程序最低要求为 1.2.3
//   1.2.3 -> 接受：版本完全一致
//   1.2.4 -> 接受：同一 MAJOR 下的 PATCH 更新
//   1.3.0 -> 接受：同一 MAJOR 下的 MINOR 更新
//   1.2.2 -> 拒绝：实现版本低于最低要求
//   1.1.9 -> 拒绝：实现版本低于最低要求
//   2.0.0 -> 拒绝：MAJOR 不同，可能存在破坏性变化

namespace
{

    using Version = std::array<unsigned int, 3>;

    std::string RequiredString(const higgsops::config::Map &map,
                               const std::string &key,
                               const std::string &context)
    {
        if (!map.Contains(key))
        {
            throw std::invalid_argument(
                context + " is missing required field: " + key);
        }
        return map[key].AsString();
    }

    Version ParseVersion(const std::string &text)
    {
        Version result{};
        std::string_view remaining(text);

        for (std::size_t index = 0; index < result.size(); ++index)
        {
            if (remaining.empty())
            {
                throw std::invalid_argument(
                    "version must use MAJOR.MINOR.PATCH: " + text);
            }

            const char *begin = remaining.data();
            const char *end = begin + remaining.size();
            const auto parsed = std::from_chars(begin, end, result[index]);
            if (parsed.ec != std::errc{} || parsed.ptr == begin)
            {
                throw std::invalid_argument(
                    "invalid numeric version component: " + text);
            }

            if (index + 1 == result.size())
            {
                if (parsed.ptr != end)
                {
                    throw std::invalid_argument(
                        "version must use MAJOR.MINOR.PATCH: " + text);
                }
            }
            else
            {
                if (parsed.ptr == end || *parsed.ptr != '.')
                {
                    throw std::invalid_argument(
                        "version must use MAJOR.MINOR.PATCH: " + text);
                }
                // 移除已经解析的部分
                remaining.remove_prefix(
                    static_cast<std::size_t>(parsed.ptr - begin) + 1);
            }
        }

        return result;
    }

} // namespace

// 判断实现版本是否满足主程序要求。
// required 表示主程序要求的最低接口版本，
// implementation 表示动态库实现所提供的接口版本。
bool IsInterfaceVersionCompatible(const std::string &required,
                                  const std::string &implementation)
{
    // 将 MAJOR.MINOR.PATCH 字符串解析成三个整数，
    // 避免使用字符串比较导致 1.10.0 小于 1.2.0 等错误。
    const Version required_version = ParseVersion(required);
    const Version implementation_version =
        ParseVersion(implementation);

    // 兼容条件：
    // 1. 实现版本与主程序要求版本的 major 必须相同；
    // 2. 实现版本必须大于或等于主程序要求的最低版本。
    //
    // 同一 major 下允许向后兼容的 minor/patch 更新，
    // 拒绝低于最低要求的旧实现和不同 major 的破坏性版本。
    return required_version[0] == implementation_version[0] &&
           implementation_version >= required_version;
}

// 检查已经创建的接口实现对象是否满足版本要求。
void ValidateInterfaceVersion(Interface &instance)
{
    // 通过虚函数取得具体实现所声明的接口版本。
    const std::string implementation_version =
        instance.GetVersion();

    // INTERFACE_VERSION 是主程序编译时要求的最低接口版本。
    // 如果实现不兼容，则抛出异常并报告双方版本。
    if (!IsInterfaceVersionCompatible(
            INTERFACE_VERSION, implementation_version))
    {
        throw std::runtime_error(
            "Interface version mismatch: consumer=" INTERFACE_VERSION
            ", implementation=" +
            implementation_version);
    }
}

const char *ToString(LoadMode mode) noexcept
{
    switch (mode)
    {
    case LoadMode::Static:
        return "static";
    case LoadMode::Dynamic:
        return "dynamic";
    }
    return "unknown";
}

HybridInterfaceLoader::HybridInterfaceLoader()
{
    // Concrete implementation knowledge is isolated in one composition root;
    // main.cpp and the generic dispatch code still know only Interface.
    RegisterBuiltInInterfaces(registry_);
}

LoadedInterface HybridInterfaceLoader::LoadFromConfig(
    const std::string &config_file,
    bool validate_version) const
{
    const higgsops::config::Node root =
        higgsops::config::LoadConfigFile(config_file);
    return Load(root.AsMap(), validate_version);
}

LoadedInterface HybridInterfaceLoader::Load(
    const higgsops::config::Map &config,
    bool validate_version) const
{
    if (!config.Contains("class"))
    {
        throw std::invalid_argument(
            "Root configuration is missing required field: class");
    }

    const higgsops::config::Node class_node = config["class"];
    const bool class_is_map = class_node.IsMap();
    std::string class_name;
    bool has_file = false;
    bool has_version = false;

    if (class_is_map)
    {
        const higgsops::config::Map class_info = class_node.AsMap();
        class_name = RequiredString(
            class_info, "class", "class configuration");
        has_file = class_info.Contains("file");
        has_version = class_info.Contains("ver");
    }
    else if (class_node.IsValue())
    {
        // Compatibility with the reference shannon_algo_db static format:
        // class: ImplA
        class_name = class_node.AsString();
    }
    else
    {
        throw std::invalid_argument(
            "Root class must be a string or a map");
    }

    LoadMode mode;
    const bool explicit_mode = config.Contains("load_mode");

    if (explicit_mode)
    {
        const std::string mode_text = RequiredString(
            config, "load_mode", "Root configuration");
        if (mode_text == "static")
        {
            mode = LoadMode::Static;
        }
        else if (mode_text == "dynamic")
        {
            mode = LoadMode::Dynamic;
        }
        else
        {
            throw std::invalid_argument(
                "load_mode must be either 'static' or 'dynamic': " +
                mode_text);
        }
    }
    else
    {
        // Backward compatibility and parity with shannon_algo_db: a class map
        // containing both file and ver is dynamic; otherwise it is static.
        // A half-specified dynamic map is rejected instead of silently falling
        // back to static loading.
        if (has_file != has_version)
        {
            throw std::invalid_argument(
                "class.file and class.ver must either both be present or both be absent");
        }
        mode = (has_file && has_version)
                   ? LoadMode::Dynamic
                   : LoadMode::Static;
    }

    std::unique_ptr<Interface> instance;
    if (mode == LoadMode::Static)
    {
        if (has_file || has_version)
        {
            throw std::invalid_argument(
                "Static configuration must not contain class.file or class.ver");
        }
        instance = registry_.Create(class_name, config);
    }
    else
    {
        if (!class_is_map)
        {
            throw std::invalid_argument(
                "Dynamic configuration requires a class map");
        }
        const higgsops::config::Map class_info = class_node.AsMap();
        (void)RequiredString(
            class_info, "file", "Dynamic class configuration");
        (void)RequiredString(
            class_info, "ver", "Dynamic class configuration");
        instance = higgsops::LoadClass<Interface>(config);
    }

    if (!instance)
    {
        throw std::runtime_error(
            "Loader returned null for class: " + class_name);
    }
    if (validate_version)
    {
        ValidateInterfaceVersion(*instance);
    }

    return LoadedInterface{
        std::move(instance), mode, std::move(class_name)};
}

LoadedInterface LoadInterfaceWithModeFromConfig(
    const std::string &config_file,
    bool validate_version)
{
    // 静态注册的接口实现类在 HybridInterfaceLoader 构造函数中注册，
    // 动态加载的接口实现类通过配置文件指定动态库路径和版本号
    HybridInterfaceLoader loader;
    return loader.LoadFromConfig(config_file, validate_version);
}
