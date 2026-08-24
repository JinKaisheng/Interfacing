# 编译可执行文件 main.out
add_executable(main.out main.cpp)
target_include_directories(main.out PRIVATE ${CMAKE_SOURCE_DIR})

# 链接 dl 库（用于动态加载）
target_link_libraries(main.out dl)

# 设置输出目录
set_target_properties(main.out PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

#等价命令:g++ -I. autotest/main.cpp -ldl -o build/bin/main.out