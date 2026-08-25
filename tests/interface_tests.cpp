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
    std::unique_ptr<Interface> instance = LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_a.yaml"));

    EXPECT_EQ(instance->GetVersion(), INTERFACE_VERSION);
    testing::internal::CaptureStdout();
    instance->print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("configured-A"),
              std::string::npos);
}

TEST(DynamicLoading, LoadsImplementationBByChangingOnlyConfiguration) {
    std::unique_ptr<Interface> instance = LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_CONFIG_DIR, "impl_b.yaml"));

    testing::internal::CaptureStdout();
    instance->print();
    EXPECT_NE(testing::internal::GetCapturedStdout().find("configured-B"),
              std::string::npos);
}

TEST(VersionValidation, RejectsIncompatibleImplementation) {
    EXPECT_THROW(
        LoadInterfaceFromConfig(
            ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml")),
        std::runtime_error);
}

TEST(VersionValidation, DisabledValidationLetsMismatchEscape) {
    std::unique_ptr<Interface> instance = LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR, "legacy.yaml"), false);

    EXPECT_EQ(instance->GetVersion(), "0.9.0");
    EXPECT_FALSE(IsInterfaceVersionCompatible(INTERFACE_VERSION,
                                              instance->GetVersion()));
}

TEST(ClassLoaderValidation, RejectsWrongDeclaredLibraryVersion) {
    EXPECT_ANY_THROW(LoadInterfaceFromConfig(
        ConfigPath(INTERFACING_TEST_CONFIG_DIR,
                   "wrong-declared-version.yaml"),
        false));
}

}  // namespace