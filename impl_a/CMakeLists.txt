# 编译动态库 libimpl_a.so
add_library(impl_a SHARED impl_a.cpp)
target_include_directories(impl_a PRIVATE ${CMAKE_SOURCE_DIR})

# 设置输出名称
set_target_properties(impl_a PROPERTIES 
    OUTPUT_NAME "impl_a"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 编译选项
target_compile_options(impl_a PRIVATE -fPIC)

#等价命令：g++ -shared -fPIC -I. impl_a/impl_a.cpp -o build/lib/libimpl_a.so