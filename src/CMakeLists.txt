# 供单元测试复用的静态库
add_library(playground_core STATIC memory/memory_pool.cpp)
target_include_directories(playground_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(playground_core PUBLIC cxx_std_20)

# 各主题的 demo，每个都是独立可执行文件
add_executable(fork_exec_demo process/fork_exec_demo.cpp)
add_executable(process_pool process/process_pool.cpp)
add_executable(raii_demo memory/raii_demo.cpp)
