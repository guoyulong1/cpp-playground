// 线程池演示：
//   1. 数据并行：把 1..N 切块提交，future 汇总（分而治之的最小样例）
//   2. 队列积压：任务比 worker 多时观察 pending_tasks 变化
//   3. 异常穿透：任务里的异常经 future.get() 传回，worker 不死、池还能用
#include "playground/thread_pool.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

long long partial_square_sum(int begin, int end) {
    long long sum = 0;
    for (int i = begin; i <= end; ++i) {
        sum += static_cast<long long>(i) * i;
    }
    return sum;
}

void parallel_sum_demo() {
    playground::ThreadPool pool(4);
    std::cout << "[pool] 启动 " << pool.thread_count() << " 个 worker\n";

    constexpr int kTotal = 100;
    constexpr int kChunks = 4;
    constexpr int kChunkSize = kTotal / kChunks;
    std::vector<std::future<long long>> futures;
    futures.reserve(kChunks);
    for (int i = 0; i < kChunks; ++i) {
        futures.push_back(
            pool.submit(partial_square_sum, i * kChunkSize + 1, (i + 1) * kChunkSize));
    }
    long long total = 0;
    for (std::future<long long>& future : futures) {
        total += future.get();  // 阻塞直到该任务完成，结果由 future 送回
    }
    std::cout << "[pool] 1..100 平方和 = " << total << "（期望 338350）\n";
}

void backlog_demo() {
    playground::ThreadPool pool(2);
    constexpr int kTasks = 6;
    std::cout << "\n[pool] 提交 " << kTasks << " 个 100ms 的任务，2 个 worker 消化，观察队列：\n";
    std::vector<std::future<int>> futures;
    futures.reserve(kTasks);
    for (int i = 0; i < kTasks; ++i) {
        futures.push_back(pool.submit([i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return i;
        }));
    }
    for (int tick = 1; tick <= 3; ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[pool] t=" << tick * 100 << "ms，待执行任务数 = " << pool.pending_tasks()
                  << '\n';
    }
    int checksum = 0;
    for (std::future<int>& future : futures) {
        checksum += future.get();
    }
    std::cout << "[pool] 全部完成，任务序号和 = " << checksum << "（期望 15）\n";
}

void exception_demo() {
    playground::ThreadPool pool(2);
    std::future<int> boom = pool.submit([]() -> int {
        throw std::runtime_error("boom");
    });
    try {
        static_cast<void>(boom.get());
    } catch (const std::runtime_error& error) {
        std::cout << "[pool] 异常经 future 传回: " << error.what() << "，worker 仍存活\n";
    }
    std::future<int> healthy = pool.submit([] { return 42; });
    std::cout << "[pool] 同一个池继续工作: " << healthy.get() << '\n';
}

}  // namespace

int main() {
    std::cout << "== 1. 数据并行汇总 ==\n";
    parallel_sum_demo();
    std::cout << "\n== 2. 队列积压 ==\n";
    backlog_demo();
    std::cout << "\n== 3. 异常穿透 ==\n";
    exception_demo();
    std::cout << "\n[pool] main 结束前，池析构会排干队列并 join 全部 worker\n";
    return 0;
}
