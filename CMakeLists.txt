cmake_minimum_required(VERSION 3.10)
project(QuantableInterface)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加子目录
add_subdirectory(impl_a)
add_subdirectory(impl_b)
add_subdirectory(autotest)