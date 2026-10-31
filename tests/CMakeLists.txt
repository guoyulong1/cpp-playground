include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
    test_memory_pool.cpp
    test_thread_pool.cpp
    test_serialization.cpp)
target_link_libraries(unit_tests PRIVATE
    playground_core
    playground_proto
    nlohmann_json::nlohmann_json
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)
