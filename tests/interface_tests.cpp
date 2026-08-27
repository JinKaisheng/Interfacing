#include "interface_loader.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

std::string ConfigPath(const char* directory, const char* file) {
    return std::string(directory) + "/" + file;
}

higgsops::config::Map ConfigFromString(const std::string& text) {
    return higgsops::config::LoadConfigFromString(text).AsMap();
}

void ExpectPrintContains(Interface& instance, const std::string& expected) {
    testing::internal::CaptureStdout();
    instance.print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find(expected),
              std::string::npos);
}

// 测试名：VersionCompatibility.FollowsSemanticVersionRules
// 场景：比较合法版本的兼容关系，并传入缺少 PATCH 的非法版本。
// 预期：兼容版本返回 true，不兼容版本返回 false，非法格式抛出 invalid_argument。
TEST(VersionCompatibility, FollowsSemanticVersionRules) {
    EXPECT_TRUE(IsInterfaceVersionCompatible("1.0.0", "1.0.0"));
    EXPECT_TRUE(IsInterfaceVersionCompatible("1.0.0", "1.2.0"));
    EXPECT_FALSE(IsInterfaceVersionCompatible("1.1.0", "1.0.9"));
    EXPECT_FALSE(IsInterfaceVersionCompatible("1.0.0", "2.0.0"));
    EXPECT_THROW(IsInterfaceVersionCompatible("1.0", "1.0.0"),
                 std::invalid_argument);
}

// 测试名：StaticLoading.LoadsImplementationAAndReportsStaticMode
// 场景：使用 impl_a_static.yaml 通过静态注册表创建 ImplA。
// 预期：对象非空，报告 Static/ImplA/正确接口版本，并取得静态业务配置。
TEST(StaticLoading, LoadsImplementationAAndReportsStaticMode) {
    HybridInterfaceLoader loader;
    LoadedInterface loaded = loader.LoadFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_a_static.yaml"));

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Static);
    EXPECT_EQ(loaded.class_name, "ImplA");
    EXPECT_EQ(loaded.instance->GetVersion(), INTERFACE_VERSION);
    ExpectPrintContains(*loaded.instance, "configured-static-A");
}

// 测试名：DynamicLoading.LoadsImplementationAAndReportsDynamicMode
// 场景：使用 impl_a_dynamic.yaml 通过 Higgs ClassLoader 加载 ImplA。
// 预期：对象非空，报告 Dynamic/ImplA，并取得动态业务配置。
TEST(DynamicLoading, LoadsImplementationAAndReportsDynamicMode) {
    HybridInterfaceLoader loader;
    LoadedInterface loaded = loader.LoadFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_a_dynamic.yaml"));

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Dynamic);
    EXPECT_EQ(loaded.class_name, "ImplA");
    ExpectPrintContains(*loaded.instance, "configured-dynamic-A");
}

// 测试名：StaticLoading.LoadsImplementationBAndReportsStaticMode
// 场景：使用 impl_b_static.yaml 通过静态注册表创建 ImplB。
// 预期：对象非空，报告 Static/ImplB，并取得静态业务配置。
TEST(StaticLoading, LoadsImplementationBAndReportsStaticMode) {
    HybridInterfaceLoader loader;
    LoadedInterface loaded = loader.LoadFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_b_static.yaml"));

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Static);
    EXPECT_EQ(loaded.class_name, "ImplB");
    ExpectPrintContains(*loaded.instance, "configured-static-B");
}

// 测试名：DynamicLoading.LoadsImplementationBAndReportsDynamicMode
// 场景：使用 impl_b_dynamic.yaml 通过 Higgs ClassLoader 加载 ImplB。
// 预期：对象非空，报告 Dynamic/ImplB，并取得动态业务配置。
TEST(DynamicLoading, LoadsImplementationBAndReportsDynamicMode) {
    HybridInterfaceLoader loader;
    LoadedInterface loaded = loader.LoadFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_b_dynamic.yaml"));

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Dynamic);
    EXPECT_EQ(loaded.class_name, "ImplB");
    ExpectPrintContains(*loaded.instance, "configured-dynamic-B");
}

// 测试名：VersionValidation.RejectsIncompatibleImplementation
// 场景：开启接口校验，动态加载接口版本为 0.9.0 的 LegacyImpl。
// 预期：统一入口抛出 runtime_error，不把不兼容对象返回给调用者。
TEST(VersionValidation, RejectsIncompatibleImplementation) {
    EXPECT_THROW(
        LoadInterfaceWithModeFromConfig(
            ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml")),
        std::runtime_error);
}

// 测试名：VersionValidation.DisabledValidationLetsMismatchEscape
// 场景：关闭接口校验，动态加载接口版本为 0.9.0 的 LegacyImpl。
// 预期：对象能够创建，但兼容性判断为 false，证明关闭校验的风险。
TEST(VersionValidation, DisabledValidationLetsMismatchEscape) {
    LoadedInterface loaded = LoadInterfaceWithModeFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml"), false);

    ASSERT_NE(loaded.instance, nullptr);
    EXPECT_EQ(loaded.mode, LoadMode::Dynamic);
    EXPECT_EQ(loaded.instance->GetVersion(), "0.9.0");
    EXPECT_FALSE(IsInterfaceVersionCompatible(INTERFACE_VERSION,
                                              loaded.instance->GetVersion()));
}

// 测试名：ClassLoaderValidation.RejectsWrongDeclaredLibraryVersion
// 场景：YAML 声明 9.9.9，实际 ImplA 插件导出 1.0.0。
// 预期：Higgs ClassLoader 在对象创建前抛出版本不匹配异常。
TEST(ClassLoaderValidation, RejectsWrongDeclaredLibraryVersion) {
    EXPECT_ANY_THROW(LoadInterfaceWithModeFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR,
                   "wrong-declared-version.yaml"),
        false));
}

// 测试名：StaticLoadingValidation.RejectsUnregisteredClass
// 场景：静态配置请求未注册的 MissingImpl。
// 预期：InterfaceRegistry 创建失败并抛出 runtime_error。
TEST(StaticLoadingValidation, RejectsUnregisteredClass) {
    HybridInterfaceLoader loader;
    const higgsops::config::Map config = ConfigFromString(
        "load_mode: static\n"
        "class:\n"
        "  class: MissingImpl\n");

    EXPECT_THROW(loader.Load(config), std::runtime_error);
}

// 测试名：ConfigurationValidation.RejectsDynamicConfigWithoutFile
// 场景：显式选择 dynamic，但 class 配置缺少 file。
// 预期：配置校验抛出 invalid_argument，不进入 ClassLoader。
TEST(ConfigurationValidation, RejectsDynamicConfigWithoutFile) {
    HybridInterfaceLoader loader;
    const higgsops::config::Map config = ConfigFromString(
        "load_mode: dynamic\n"
        "class:\n"
        "  ver: 1.0.0\n"
        "  class: ImplA\n");

    EXPECT_THROW(loader.Load(config), std::invalid_argument);
}

// 测试名：ConfigurationValidation.RejectsUnknownLoadMode
// 场景：配置使用未知的 load_mode: surprise。
// 预期：配置校验抛出 invalid_argument。
TEST(ConfigurationValidation, RejectsUnknownLoadMode) {
    HybridInterfaceLoader loader;
    const higgsops::config::Map config = ConfigFromString(
        "load_mode: surprise\n"
        "class:\n"
        "  class: ImplA\n");

    EXPECT_THROW(loader.Load(config), std::invalid_argument);
}

// 测试名：ConfigurationValidation.StaticModeRejectsDynamicFields
// 场景：显式选择 static，却同时提供 class.file 和 class.ver。
// 预期：配置校验抛出 invalid_argument，拒绝含糊配置。
TEST(ConfigurationValidation, StaticModeRejectsDynamicFields) {
    HybridInterfaceLoader loader;
    const higgsops::config::Map config = ConfigFromString(
        "load_mode: static\n"
        "class:\n"
        "  file: ignored.so\n"
        "  ver: 1.0.0\n"
        "  class: ImplA\n");

    EXPECT_THROW(loader.Load(config), std::invalid_argument);
}

}  // namespace
