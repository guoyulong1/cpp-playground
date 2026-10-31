# 第三方依赖统一管理（FetchContent）：
#   CI（境外网络）：cmake configure 时直接从 GitHub 下载，带 SHA256 校验；
#   本地（被墙网络）：先跑 scripts/fetch-deps-cn.sh 预热源码，再用 `cmake --preset cn` 配置，
#                     FETCHCONTENT_SOURCE_DIR_<NAME> 指向本地源码，完全离线。
# 选型均为高 star、工业界广泛使用的库，对应四岗位能力：
#   fmt(20k+) / spdlog(25k+) / nlohmann-json(46k+) / cpp-httplib(14k+) / libzmq+cppzmq(9.6k+)
include(FetchContent)

# ---------- fmt：现代格式化库（C++20 std::format 的工业前身） ----------
FetchContent_Declare(
    fmt
    URL https://github.com/fmtlib/fmt/releases/download/12.2.0/fmt-12.2.0.zip
    URL_HASH SHA256=a2f4a8d51178f954e4c339007f77edd76ba0cb2e36f87a48e5a5403d9be5878f
)

# ---------- spdlog：结构化日志（默认用自带 fmt，编译快） ----------
FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
    URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
)

# ---------- nlohmann-json：JSON 的事实标准 ----------
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)

# ---------- cpp-httplib：单头 HTTP server/client（mini HTTP 练习的对照组） ----------
FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.63.0.tar.gz
    URL_HASH SHA256=44ab42e16926e95f7641d286bc90d739ab1c16886114d7cc5bc7a9f905c5edf8
)

# ---------- libzmq：异步消息库（中间件设计的最佳入门实现） ----------
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURVE OFF CACHE BOOL "" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "" FORCE)
set(WITH_LIBSODIUM OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libzmq
    URL https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz
    URL_HASH SHA256=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43
)

FetchContent_MakeAvailable(fmt spdlog nlohmann_json httplib libzmq)

# ---------- cppzmq：libzmq 的现代 C++ 封装（header-only，接口层直接引用源码头文件） ----------
FetchContent_Declare(
    cppzmq
    URL https://github.com/zeromq/cppzmq/archive/refs/tags/v4.11.0.tar.gz
    URL_HASH SHA256=0fff4ff311a7c88fdb76fceefba0e180232d56984f577db371d505e4d4c91afd
)
FetchContent_GetProperties(cppzmq)
if(NOT cppzmq_POPULATED)
    FetchContent_Populate(cppzmq)
    # 不 add_subdirectory（其 CMake 强依赖 find_package(ZeroMQ)，与本仓库源码构建方式冲突），
    # 直接暴露头文件目录 + 链接 libzmq-static，这就是 cppzmq 的全部。
    add_library(cppzmq INTERFACE)
    target_include_directories(cppzmq INTERFACE ${cppzmq_SOURCE_DIR}/include)
    target_link_libraries(cppzmq INTERFACE libzmq-static)
endif()
