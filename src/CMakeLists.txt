# 供单元测试与各 demo 复用的静态库
add_library(playground_core STATIC
    memory/memory_pool.cpp
    thread/thread_pool.cpp
    support/logger.cpp)
target_include_directories(playground_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(playground_core PUBLIC cxx_std_20)
target_link_libraries(playground_core PUBLIC Threads::Threads spdlog::spdlog)

# 各主题的 demo，每个都是独立可执行文件
add_executable(fork_exec_demo process/fork_exec_demo.cpp)
add_executable(fork_cost_demo process/fork_cost_demo.cpp)
add_executable(process_pool process/process_pool.cpp)
add_executable(raii_demo memory/raii_demo.cpp)
add_executable(memory_optimization_demo memory/optimization_demo.cpp)
target_link_libraries(memory_optimization_demo PRIVATE playground_core)
add_executable(algorithm_optimization_demo algo/optimization_demo.cpp)
add_executable(thread_pool_demo thread/thread_pool_demo.cpp)
target_link_libraries(thread_pool_demo PRIVATE playground_core)
add_executable(echo_server network/echo_server.cpp)

# ---------- 网络进阶：可观测 REST 服务（cpp-httplib + json + 手写 /metrics） ----------
add_executable(http_server network/http_server.cpp)
target_link_libraries(http_server PRIVATE playground_core httplib nlohmann_json::nlohmann_json)

# ---------- 中间件：序列化 + 消息模式 ----------
# protobuf 代码生成：telemetry.proto → telemetry.pb.{h,cc}（输出进构建目录）
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/middleware/proto/telemetry.proto)
add_library(playground_proto STATIC ${PROTO_SRCS})
target_include_directories(playground_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(playground_proto PUBLIC protobuf::libprotobuf)

add_executable(serialization_benchmark middleware/serialization_benchmark.cpp)
target_link_libraries(serialization_benchmark PRIVATE playground_proto nlohmann_json::nlohmann_json)

add_executable(zmq_patterns_demo middleware/zmq_patterns_demo.cpp)
target_link_libraries(zmq_patterns_demo PRIVATE cppzmq)

# ---------- ROS2 模块（条件启用，见顶层 CMakeLists） ----------
if(PLAYGROUND_WITH_ROS2)
    add_subdirectory(ros2)
endif()
