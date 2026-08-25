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