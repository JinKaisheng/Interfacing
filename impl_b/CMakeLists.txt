add_library(impl_b SHARED impl_b.cpp)
target_include_directories(impl_b PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(impl_b PRIVATE ${INTERFACING_HIGGS_LIBRARIES})
set_target_properties(impl_b PROPERTIES
    OUTPUT_NAME "impl_b"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/lib")
install(TARGETS impl_b LIBRARY DESTINATION lib)

# 这段 CMake 把 impl_b.cpp 定义为一个独立插件目标，接入公共接口和 HiggsOps 依赖，
# 并把动态库的构建位置及安装位置固定下来，使配置文件能够稳定找到它。