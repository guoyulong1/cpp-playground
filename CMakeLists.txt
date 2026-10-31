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

# 线程池（src/thread/）需要 pthread
find_package(Threads REQUIRED)

# 第三方依赖（fmt/spdlog/nlohmann-json/cpp-httplib/libzmq+cppzmq）
#   CI：configure 时直接从 GitHub 下载；被墙网络：scripts/fetch-deps-cn.sh + preset cn
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdParty.cmake)

# Protobuf：走系统安装（apt 的 protobuf-compiler / libprotobuf-dev；CI 同样 apt 安装）
find_package(Protobuf 3.12 REQUIRED)

# ROS2 模块条件启用：preset 的 CMAKE_PREFIX_PATH 指向 /opt/ros/humble 即可被检测。
# ament 构建系统还依赖几个环境变量（平时由 source setup.bash 设置）：
# 这里补齐 PYTHONPATH / AMENT_PREFIX_PATH / LD_LIBRARY_PATH，让「未 source 的 shell」也能配置。
# 已 source 的环境不受影响（路径重复无害）。无 ROS2 环境自动跳过，不影响其余模块。
if(EXISTS /opt/ros/humble)
    set(ENV{PYTHONPATH} "/opt/ros/humble/lib/python3.10/site-packages:$ENV{PYTHONPATH}")
    set(ENV{AMENT_PREFIX_PATH} "/opt/ros/humble:$ENV{AMENT_PREFIX_PATH}")
    set(ENV{LD_LIBRARY_PATH} "/opt/ros/humble/lib:$ENV{LD_LIBRARY_PATH}")
endif()
find_package(rclcpp QUIET)
if(rclcpp_FOUND)
    # std_msgs/std_srvs 与 rclcpp 同级声明（消息包是独立的 ament 包，不是 rclcpp 自带）
    find_package(std_msgs QUIET)
    find_package(std_srvs QUIET)
    if(std_msgs_FOUND AND std_srvs_FOUND)
        message(STATUS "检测到 rclcpp：启用 ROS2 模块（src/ros2/）")
        set(PLAYGROUND_WITH_ROS2 ON)
    else()
        message(STATUS "rclcpp 可用但缺 std_msgs/std_srvs：跳过 ROS2 模块")
    endif()
else()
    message(STATUS "未检测到 rclcpp：跳过 ROS2 模块（source /opt/ros/humble/setup.bash 后重新 configure 可启用）")
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
