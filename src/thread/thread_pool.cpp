#include "playground/thread_pool.hpp"

#include <utility>

namespace playground {

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool: thread_count must be > 0");
    }
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        // 成员函数 this 指针作捕获：worker 直接访问共享队列与条件变量
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;  // 改共享状态必须持锁，否则与 wait 的判断存在竞态
    }
    cv_.notify_all();  // 唤醒所有 worker，让它们看到 stopping 后退出
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 谓词版 wait：虚假唤醒与「先 notify 后 wait」都能被正确处理
            cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;  // 排干队列再退出：已提交的任务不丢
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();  // 在锁外执行：长任务不能挡住 submit() 和其他 worker
        // 任务抛出的异常已被 packaged_task 捕获存入 future，这里不会逃逸
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("ThreadPool: submit on stopped pool");
        }
        tasks_.push(std::move(task));
    }
    cv_.notify_one();  // 锁外 notify：避免唤醒的 worker 立刻撞上还没释放的锁
}

std::size_t ThreadPool::pending_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

}  // namespace playground
