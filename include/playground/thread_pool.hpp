#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace playground {

// 固定线程数的线程池（mutex + condition_variable 版本，演进路线见 docs/thread-pool.md）：
//   submit() 把可调用对象打包成 packaged_task 投入队列并返回 future；
//   worker 线程循环取任务，在锁外执行，任务异常经 future 传回调用方；
//   析构时先置 stopping 再唤醒全部 worker，排干队列后逐个 join（已提交的任务不会丢）。
// 练习重点：虚假唤醒、notify 与锁的配合、packaged_task 为什么进不了 std::function。
class ThreadPool {
public:
    // thread_count 为 0 时抛 std::invalid_argument
    explicit ThreadPool(std::size_t thread_count);

    // 排干队列并 join 所有 worker
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务，返回 future<返回值类型>；池析构开始后 submit 抛 std::runtime_error。
    // 参数会被拷贝/移动进任务（与 std::bind 语义一致），需要引用请显式传 std::ref。
    template <typename F, typename... Args>
    auto submit(F&& func, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

    std::size_t thread_count() const noexcept { return workers_.size(); }

    // 队列中尚未开始执行的任务数（不含正在运行的）
    std::size_t pending_tasks();

private:
    void worker_loop();
    void enqueue(std::function<void()> task);

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

template <typename F, typename... Args>
auto ThreadPool::submit(F&& func, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using Result = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

    // packaged_task 只能移动，而 std::function 要求可拷贝，
    // 惯用法是把它包进 shared_ptr 再塞进 lambda（练习：想想还能怎么类型擦除？）
    auto task = std::make_shared<std::packaged_task<Result()>>(
        [callable = std::forward<F>(func),
         ...bound = std::forward<Args>(args)]() mutable -> Result {
            return std::invoke(std::move(callable), std::move(bound)...);
        });
    std::future<Result> future = task->get_future();
    enqueue([task] { (*task)(); });
    return future;
}

}  // namespace playground
