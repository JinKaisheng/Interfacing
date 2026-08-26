#include "interface_loader.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

std::string ConfigPath(const char* directory, const char* file) {
    return std::string(directory) + "/" + file;
}

// 验证接口版本兼容规则
TEST(VersionCompatibility, FollowsSemanticVersionRules) {
    EXPECT_TRUE(IsInterfaceVersionCompatible("1.0.0", "1.0.0"));
    EXPECT_TRUE(IsInterfaceVersionCompatible("1.0.0", "1.2.0"));
    EXPECT_FALSE(IsInterfaceVersionCompatible("1.1.0", "1.0.9"));
    EXPECT_FALSE(IsInterfaceVersionCompatible("1.0.0", "2.0.0"));
    EXPECT_THROW(IsInterfaceVersionCompatible("1.0", "1.0.0"),
                 std::invalid_argument);
}

// 验证能够从 YAML 加载 ImplA
TEST(DynamicLoading, LoadsImplementationAFromConfiguration) {
    std::unique_ptr<Interface> instance = LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_a.yaml"));

    EXPECT_EQ(instance->GetVersion(), INTERFACE_VERSION);
    testing::internal::CaptureStdout();
    instance->print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("configured-A"),
              std::string::npos);
}

// 验证只修改配置即可切换到 ImplB
TEST(DynamicLoading, LoadsImplementationBByChangingOnlyConfiguration) {
    std::unique_ptr<Interface> instance = LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_b.yaml"));

    testing::internal::CaptureStdout();
    instance->print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("configured-B"),
              std::string::npos);
}

// 验证主程序会拒绝旧接口实现
TEST(VersionValidation, RejectsIncompatibleImplementation) {
    EXPECT_THROW(
        LoadInterfaceFromConfig(
            ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml")),
        std::runtime_error);
}

// 证明关闭接口校验会产生风险
TEST(VersionValidation, DisabledValidationLetsMismatchEscape) {
    std::unique_ptr<Interface> instance = LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml"), false);

    EXPECT_EQ(instance->GetVersion(), "0.9.0");
    EXPECT_FALSE(IsInterfaceVersionCompatible(INTERFACE_VERSION,
                                              instance->GetVersion()));
}

// 验证 YAML 与动态库自身版本不一致时会被拒绝
TEST(ClassLoaderValidation, RejectsWrongDeclaredLibraryVersion) {
    EXPECT_ANY_THROW(LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR,
                   "wrong-declared-version.yaml"),
        false));
}

}  // namespace