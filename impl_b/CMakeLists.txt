# 编译动态库 libimpl_b.so
add_library(impl_b SHARED impl_b.cpp)
target_include_directories(impl_b PRIVATE ${CMAKE_SOURCE_DIR})

# 设置输出名称
set_target_properties(impl_b PROPERTIES 
    OUTPUT_NAME "impl_b"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 编译选项
target_compile_options(impl_b PRIVATE -fPIC)

#等价命令：g++ -shared -fPIC -I. impl_b/impl_b.cpp -o build/lib/libimpl_b.so