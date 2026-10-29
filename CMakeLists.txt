cmake_minimum_required(VERSION 3.21)

project(cpp_playground
    VERSION 0.1.0
    DESCRIPTION "C++ 练手场：进程管理 / 内存管理 / clang-tidy / Code Review / CI-CD"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，clang-tidy / clangd 都依赖它
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 所有可执行文件统一输出到构建目录根，方便 ./build/default/xxx 直接运行
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(PLAYGROUND_WARNINGS_AS_ERRORS "把编译警告视为错误" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(PLAYGROUND_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
