#include "playground/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

int square_with_nap(int value) {
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    return value * value;
}

TEST(ThreadPoolTest, SubmitReturnsResults) {
    playground::ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    futures.reserve(100);
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit(square_with_nap, i));
    }
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(futures[static_cast<std::size_t>(i)].get(), i * i);
    }
}

TEST(ThreadPoolTest, ArgumentsAreBoundIntoTask) {
    playground::ThreadPool pool(1);
    int value = 0;
    std::future<void> future = pool.submit([](int* out, int factor) { *out = factor * 21; },
                                           &value, 2);
    future.get();
    EXPECT_EQ(value, 42);
}

TEST(ThreadPoolTest, ExceptionPropagatesThroughFuture) {
    playground::ThreadPool pool(1);
    std::future<int> future = pool.submit([]() -> int {
        throw std::runtime_error("boom");
    });
    EXPECT_THROW(static_cast<void>(future.get()), std::runtime_error);

    // worker 不受影响，后续任务照常执行
    std::future<int> next = pool.submit([] { return 7; });
    EXPECT_EQ(next.get(), 7);
}

TEST(ThreadPoolTest, ZeroThreadCountThrows) {
    EXPECT_THROW(playground::ThreadPool(0), std::invalid_argument);
}

TEST(ThreadPoolTest, PendingTasksReflectQueueDepth) {
    playground::ThreadPool pool(1);
    std::promise<void> started;
    std::future<void> started_future = started.get_future();
    std::future<void> blocker = pool.submit([&started] {
        started.set_value();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });
    started_future.wait();  // worker 一定正卡在 blocker 上

    std::future<int> first = pool.submit([] { return 1; });
    std::future<int> second = pool.submit([] { return 2; });
    EXPECT_EQ(pool.pending_tasks(), std::size_t{2});
    EXPECT_EQ(first.get(), 1);
    EXPECT_EQ(second.get(), 2);
    static_cast<void>(blocker.get());
    EXPECT_EQ(pool.pending_tasks(), std::size_t{0});
}

TEST(ThreadPoolTest, DestructorDrainsPendingTasks) {
    std::atomic<int> done{0};
    {
        playground::ThreadPool pool(1);
        for (int i = 0; i < 20; ++i) {
            pool.submit([&done] {
                ++done;
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            });
        }
    }  // 析构：置 stopping → 唤醒 worker → 排干队列 → join
    EXPECT_EQ(done.load(), 20);
}

}  // namespace
