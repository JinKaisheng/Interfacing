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
"load_mode: dynamic\nclass:\n  file: $<TARGET_FILE:legacy_impl>\n  ver: 0.9.0\n  class: LegacyImpl\nmessage: legacy\n")

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/test-config/wrong-declared-version.yaml"
    CONTENT
"load_mode: dynamic\nclass:\n  file: $<TARGET_FILE:impl_a>\n  ver: 9.9.9\n  class: ImplA\nmessage: wrong-version\n")

add_executable(interfacing_tests interface_tests.cpp)
target_link_libraries(interfacing_tests PRIVATE
    interfacing_loader gtest gtest_main)
target_compile_definitions(interfacing_tests PRIVATE
    INTERFACING_CONFIG_DIR="${CMAKE_BINARY_DIR}/config"
    INTERFACING_TEST_CONFIG_DIR="${CMAKE_BINARY_DIR}/test-config")
add_dependencies(interfacing_tests
    impl_a impl_b impl_a_static impl_b_static legacy_impl)
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
