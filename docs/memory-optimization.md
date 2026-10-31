# 内存优化学习方案

> 承接 README「内存管理」主题（RAII / 智能指针 / MemoryPool）的进阶路线。
> 核心思路：**先看见内存（工具观测），再减少分配（写法），最后改变分配方式（策略）**。
> 仓库对应：`src/memory/optimization_demo.cpp`（可直接跑的四个实验）、`include/playground/memory_pool.hpp`。

## 学习目标

完成后你应该能不查资料回答：

1. 为什么 `struct { char; double; char; int; }` 是 24 字节而重排后是 16 字节？
2. `vector::push_back` 十万次发生了多少次堆分配？`reserve` 之后呢？
3. 哪些 `std::string` 操作不碰堆？SSO 的容量是多少？
4. 什么时候自定义内存池是赚的，什么时候是亏的？
5. ASan、valgrind、heaptrack 分别回答什么问题？

## 阶段 1：看见内存（观测优先，先测后改）

| 知识点 | 说明 | 动手 |
|---|---|---|
| 对齐与填充 | `alignof`、成员按对齐要求降序排列 | 跑 demo 第 1 节；给 `BadLayout` 手算 padding 再对答案 |
| 分配计数 | 重载全局 `operator new` 打点 | 读 demo 顶部实现；这是教学手段，真实工程用工具 |
| 布局锁定 | `static_assert(sizeof(X) == N)` 防布局意外漂移 | 给 `GoodLayout` 加一条 |
| pahole | 查看 struct 实际布局与空洞 | `sudo apt install dwarves && pahole ./build/default/raii_demo` |

- [ ] 把 `BadLayout`/`GoodLayout` 各 new 一百万个进 `vector`，用 `/usr/bin/time -v` 对比最大驻留内存（RSS）

## 阶段 2：减少分配（写法层面的免费优化）

| 知识点 | 说明 | 动手 |
|---|---|---|
| `reserve` | 已知规模先占位，避免 2 倍扩容搬家的连锁拷贝 | demo 第 2 节：18 次 vs 1 次 |
| SSO | 短字符串（libstdc++ ≤ 15 字符）存在对象内部，构造/析构零堆开销 | demo 第 3 节 |
| 传参选择 | `string_view` 读、`const string&` 兼读兼存、按值 + move 接管所有权 | 写三个 `set_name`，用分配计数器对比调用成本 |
| RVO / NRVO | 按值返回局部对象通常零拷贝 | 返回 `std::string` 与 `std::move(local)` 对比——后者反而更慢，为什么？ |
| 拼接 | 循环 `+=` 是 O(总量)；`ostringstream` 有流开销 | 用 `reserve + append` 重写一段拼 JSON 的代码 |

## 阶段 3：改变分配方式（策略层面）

| 知识点 | 说明 | 动手 |
|---|---|---|
| 固定块池化 | 已有 `MemoryPool`：空闲链表 O(1)、一次大块向系统要内存 | demo 第 4 节：加速 ~8x |
| 池的适用边界 | 块大小固定、内存常驻；变长/低频分配场景反而复杂 | 给 `MemoryPool` 加 `allocate_from_pool_or_new` 回退策略 |
| `std::pmr` | `monotonic_buffer_resource`：栈/成员缓冲当堆用，整批一次释放 | `std::pmr::vector<int>` 配 `std::array<std::byte, 64*1024>` 后缓冲，数分配次数 |
| 自定义 allocator | 满足 C++ allocator 接口，容器无感换后端 | 参考 `std::scoped_allocator_adaptor`；写一个统计型 allocator |

- [ ] 实现 `MemoryPool::allocate()` 池满自动扩容（提示：扩容块要能链到空闲链表，析构时按块释放）
- [ ] `deallocate()` 加越界与重复归还检测（提示：块头藏 magic number，或维护 bitmap）

## 阶段 4：缓存友好（内存层级视角）

> 与 [算法改良方案](algorithm-optimization.md) 阶段 4 是同一件事的两面。

| 知识点 | 说明 | 动手 |
|---|---|---|
| cacheline | 64 字节一行；顺序访问 = 一次内存搬运 16 个 int | 算法 demo 第 3 节：按行 vs 按列差 ~5x |
| AoS vs SoA | 只遍历部分字段时，SoA 少搬无效数据 | 把 `struct Particle { double x,y,z; int id; }` 数组改 SoA，重写求和 |
| 分块（tiling） | 大矩阵按 L2 大小切块访问 | 把按列求和改 `block 64x64`，看能挽回多少 |
| 假共享 | 两个线程写同一 cacheline 的不同变量，缓存一致性协议互相打架 | `std::vector<std::atomic<int>>` 计数 vs `alignas(64)` 每线程计数，thread_pool_demo 思路 |

## 阶段 5：度量方法论（让数字可信）

1. **永远用 release 构建**测性能：`cmake --preset release`（Debug 的差距会失真放大）。
2. 多次取样取中位数，预热一次（填页表、热缓存）。
3. `perf stat -d ./demo` 看 cache-misses、IPC；`perf record + report` 找热点。
4. 引入 [Google Benchmark](https://github.com/google/benchmark)（FetchContent 方式同 GoogleTest），把 MemoryPool vs new/delete 写成正式 benchmark。

## 工具箱

```bash
cmake --preset asan && cmake --build --preset asan && ./build/asan/xxx   # 越界/UAF/泄漏
valgrind --leak-check=full --show-leak-kinds=all ./build/default/xxx    # 泄漏（慢 10-50x，不与 ASan 同用）
heaptrack ./build/default/xxx && heaptrack_print heaptrack.xxx.gz       # 堆剖析：谁分配的、峰值多少
perf stat -d ./build/release/xxx                                        # 缓存命中/未命中
pahole ./build/default/xxx                                              # struct 布局与空洞
/usr/bin/time -v ./build/release/xxx                                    # RSS 峰值
```

## 常见坑

- **Debug 构建下结论**：优化建议全部基于 release 数据，Debug 只用来调逻辑。
- **过度优化**：分配次数少 ≠ 快（池化引入的间接层可能在低频场景更慢），回到阶段 5 用数据说话。
- **`-ffast-math`**：能换 10% 浮点性能，代价是违反 IEEE 语义（本仓库算法 demo 的矩阵实验正是靠「浮点加法不可结合」挡住编译器换序）。
- **智能指针不是银弹**：`shared_ptr` 控制块也是一次堆分配、原子计数有缓存代价；能用 `unique_ptr`/栈对象就用。

## 延伸阅读

- 《Effective Modern C++》条款 14/29/41（move、constexpr、make_shared）
- 《What Every Programmer Should Know About Memory》（Ulrich Drepper，前 3 章）
- Carl Cook: *When a Microsecond Is an Eternity*（CppCon 演讲，极端低延迟下的内存取舍）
- CPPCON Anthony Williams / others：`std::pmr` 实战演讲
