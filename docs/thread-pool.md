# 线程池学习方案

> 回答「线程池如何写」：从最朴素的「每任务一线程」出发，一步步长出生产可用的线程池。
> 仓库对应：`include/playground/thread_pool.hpp`（v2 实现）、`src/thread/thread_pool.cpp`、
> `src/thread/thread_pool_demo.cpp`（三个演示）、`tests/test_thread_pool.cpp`（六个单测）。

## 学习目标

1. 手写一个 mutex + condition_variable 的任务队列，解释每一行为什么那么写。
2. 说清 `packaged_task` / `promise` / `future` 三者关系。
3. 处理好停止时序：已提交的任务丢不丢？正在跑的任务等不等？
4. 知道下一步往哪优化（work stealing、无锁队列、协程）。

## 演进路线（每一版都是前一版的「最小修复」）

### v0：thread-per-task（反面教材）

每个任务 `std::thread(callable).detach()`。三个致命伤：

1. 线程创建/销毁是系统调用级开销（微秒级），任务本身可能才纳秒级；
2. 无上限并发：1 万个任务 = 1 万个线程，内存（每线程默认 8MB 栈）与调度器双双爆炸；
3. `detach()` 后失去控制，进程退出时未跑完的任务直接蒸发。

- [ ] 写一个 v0：for 循环 10000 次 detach，观察线程数与内存（`top -H -p $(pgrep xxx)`）

### v1：固定 worker + 共享队列（最小可用版）

```cpp
std::vector<std::thread> workers_;      // 启动时一次创建、常驻
std::queue<std::function<void()>> tasks_;
std::mutex mutex_;                      // 保护队列与 stopping_
std::condition_variable cv_;            // 「有活干了 / 要收工了」的唤醒
bool stopping_ = false;
```

worker 循环只有三步：加锁 → **谓词 wait** → 取任务、解锁、锁外执行：

```cpp
cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });  // 为什么必须带谓词？
if (stopping_ && tasks_.empty()) return;                          // 排干再退
```

必须能回答的三个「为什么」：

1. **为什么 wait 要谓词**：挡住虚假唤醒；也挡住「notify 先于 wait」时信号被吃掉（回来检查条件不满足继续睡）。
2. **为什么任务在锁外执行**：长任务持锁会让 `submit()` 和其他 worker 全部卡死。
3. **为什么 notify 在解锁后**：唤醒的线程立刻抢锁，锁还攥在手里就是白唤。

- [ ] 把 v1 谓词改成 `if (cv_.wait_for(...))` 的裸写法，用 TSan 或高并发压出问题

### v2：返回值与异常（= 仓库当前实现）

`std::function<void()>` 装不下 `packaged_task`（后者只能移动，前者要求可拷贝），
惯用法是 `shared_ptr<packaged_task>` 包一层。核心代码在 `thread_pool.hpp` 的 `submit()`：

```cpp
auto task = std::make_shared<std::packaged_task<Result()>>(...);
std::future<Result> future = task->get_future();
enqueue([task] { (*task)(); });   // lambda 拷贝的是 shared_ptr
```

收益：任务抛异常 → 存进 future → `future.get()` 在调用方重新抛出，**worker 永不死**（demo 第 3 节）。

- [ ] 读 `submit()`，解释 `...bound = std::forward<Args>(args)` 这个 C++20 包捕获在做什么
- [ ] 传一个 `std::string` 参数给任务，任务内修改它，观察调用方是否受影响（值语义）；再试 `std::ref`

### v3：停止语义（析构是线程池最难的部分）

仓库实现的顺序：**置 stopping_（持锁）→ notify_all → join 全部**。语义是「排干」：
worker 看到 `stopping_ && tasks_.empty()` 才退出，已提交的任务一定跑完。
这是单测 `DestructorDrainsPendingTasks` 验证的行为。

要能回答：

1. 为什么 `stopping_ = true` 必须持锁？（不持锁：worker 可能刚检查完条件、还没睡下，notify 落空 → 永睡）
2. 「立即丢弃剩余任务」的池，析构该怎么改？（提示：stopping 时清空队列；`future` 会收到 `broken_promise`）
3. 析构里如果某个任务又 `submit()` 了新任务会发生什么？（当前实现抛 `runtime_error`，读 `enqueue()`）

### v4：进阶方向（按兴趣选做）

| 方向 | 思路 | 参考 |
|---|---|---|
| work stealing | 每 worker 一条本地队列，偷别队尾部，减少锁争用 | Chase-Lev deque；C++ Concurrency in Action ch9 |
| 无锁队列 | MPMC ring buffer（`moodycamel::ConcurrentQueue` 的思路） | 你真的需要吗？先测锁是不是瓶颈 |
| 批量提交 | 一次入队一批任务，摊薄锁与唤醒成本 | — |
| 优先级 | `priority_queue` 换掉 FIFO | 注意饥饿 |
| 动态扩缩 | 空闲超时回收线程、忙时补充 | Java ForkJoinPool / Go GMP 的思路 |
| 协程化 | C++20 coroutine 的 `executor` 语义 | libunifex / std::execution |

- [ ] 写一个带「立即停止」标志（不排干）的 `stop_now()`，配套单测验证队列剩余任务被丢弃
- [ ] 给 ThreadPool 加 `wait_all()`：所有任务完成后返回（提示：计数器 + 原子 + 一次额外 notify）

## 线程数怎么定

- **CPU 密集**：`std::thread::hardware_concurrency()`（≈ 核数），多了反而上下文切换；
- **IO 密集**：核数 × (1 + 等待时间/计算时间)，可以明显更多；
- 经验优先，压测验证：用不同线程数跑同一个任务集，画吞吐曲线。

## 工具与验证

```bash
cmake -B build/tsan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread" && cmake --build -j build/tsan
./build/tsan/unit_tests        # 数据竞争检测（ASan 查内存，TSan 查竞态，不可同用）
top -H -p $(pgrep thread_pool_demo)   # 观察线程数
perf lock ./build/release/thread_pool_demo 2>/dev/null  # 锁争用
```

- [ ] 用 TSan preset 跑全部单测（可照 asan preset 在 CMakePresets.json 里加一个 tsan preset）
- [ ] 压测：4 worker 提交 100 万个空任务，测吞吐（任务/秒），再改 8 worker 对比

## 常见坑

- **`submit` 里同步等自己的结果**：任务里 `future.get()` 等一个必须由本线程执行的任务 → 死锁。
- **捕获引用逃逸**：lambda 按引用捕获局部变量，任务执行时局部早已析构 → UAF（用 ASan 亲手复现一次）。
- **detach**：宁可 join，绝不 detach（教学语境）。
- **锁内做重活**：enqueue 时 `notify_one` 放锁外；任务执行放锁外。
- **忘记虚假唤醒**：任何不带谓词的 `wait` 都是隐患。

## 延伸阅读

- 《C++ Concurrency in Action》2e：ch4（future/promise）、ch9（线程池、work stealing）——本主题主线书
- `std::jthread` / `std::stop_token`（C++20）：更现代的线程生命周期管理，可作为练习把池改用 jthread
- Björn Fahller: *The Vedas: 2 thread pools*；Folly Executor 源码导读（生产级长什么样）
