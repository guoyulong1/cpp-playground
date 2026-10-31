# 算法改良学习方案

> 回答「如何改良算法」：从暴力解出发，沿着**复杂度 → 数据结构 → 常数因子 → 访存模式 → 并行**的阶梯逐级爬升，
> 每一步都先测量、后动手、再验证正确性。
> 仓库对应：`src/algo/optimization_demo.cpp`（三组对照实验，release 下差距 2x ~ 160x）。

## 核心方法论（先于一切技巧）

1. **先写对再写快**：暴力解是参照物，改良解必须和它对答案（demo 里每个实验都有 `正确性: OK`）。
2. **先测量再动手**：用计时/perf 确认热点，不优化猜测中的瓶颈。
3. **一次只改一件事**，立刻复测——否则你不知道提升从哪来。
4. **知道输入规模**：n=100 时 O(n²) 可能比 O(n log n) 更快（常数小），改良要匹配真实规模。

## 阶段 1：降低复杂度（换算法）

| 知识点 | 说明 |
|---|---|
| 大 O 只看主导项 | O(n²) → O(n)：两数之和用哈希表「见过谁、还缺谁」一次扫完（demo 第 1 节，~75x） |
| 摊还分析 | vector 翻倍扩容单次 O(n)，摊还 O(1)；`sort` O(n log n) vs 冒泡 O(n²) |
| 最好的数据结构换挡 | 排序→`nth_element` 只求第 k 大；查找→哈希/二分；区间→前缀和/差分 |

动手：
- [ ] 在 demo 里加第三种解法：先 `sort` 再双指针 O(n log n)，与哈希版对比谁快
- [ ] 写一个 O(n²) 的矩阵乘法，只用交换循环顺序（ikj 代替 ijk）拿到缓存收益（见阶段 4）

## 阶段 2：选对数据结构（容器代价意识）

| 容器 | 查找 | 中间插入 | 内存布局 | 缓存友好 |
|---|---|---|---|---|
| `vector` | O(n) 线性 / O(log n) 有序二分 | O(n) 搬移 | 连续 | ★★★ |
| `deque` | O(n) | 两端 O(1) | 分段连续 | ★★ |
| `list` | O(n) | O(1)（已知位置） | 节点离散 | ★（每跳一次 miss） |
| `map`（红黑树） | O(log n) | O(log n) | 节点离散 | ★★ |
| `unordered_map` | O(1) 平均 | O(1) 平均 | 桶+链 | ★★（哈希本身有计算成本） |

关键认知：**「list 插入快」在缓存面前常常是幻觉**——先 O(n) 找到位置，再付出每节点一次 miss 的代价。

动手：
- [ ] 用 `vector` vs `list` 各做 10 万次「尾部追加 + 全表求和」，release 下计时对比
- [ ] 把 `two_sum_brute` 里的内层线性查找换成「先排序 + 二分」，对比哈希版

## 阶段 3：压常数因子（同复杂度下的写法优化）

| 手法 | 说明 | 仓库示例 |
|---|---|---|
| 减少分配 | `reserve` 一次到位，消掉 2 倍扩容的连锁拷贝 | demo 第 2 节，~2.3x |
| 消除不必要拷贝 | `const&` 入、`move` 出、原地算法 `std::transform` | — |
| 循环不变量外提 | 长度、指针、下标换算提出循环 | — |
| 分支友好 | 可预测分支近乎免费，不可预测分支 ~20 周期；先 `sort` 再过滤有时更快 | 见下方练习 |
| 用对类型 | `int` 索引优于 `size_t` 溢出检查？`float` 4B 带宽减半 | — |

动手：
- [ ] 写两个版本：对 `vector<int>` 累加「大于 128 的数」，一个乱序一个先 sort，对比（分支预测经典实验）
- [ ] 把 `concat_naive` 改成 `text.append(piece.data(), piece.size())`，看还能不能再快

## 阶段 4：访存模式（内存墙，与 [内存优化](memory-optimization.md) 阶段 4 交叉）

同是 O(n²)，按行遍历比按列快 ~5x（demo 第 3 节）——CPU 的速度差距远小于内存层级差距。

- 顺序访问 > 跳跃访问；紧凑数组 > 指针追逐。
- 分块（loop tiling）：按 L1/L2 容量切子矩阵，让工作集常驻缓存。
- **小心编译器「帮你优化」**：demo 里整数求和会被 GCC 合法地循环交换，两个版本测不出差距；
  用 float 矩阵 + double 累加（浮点不可结合）挡住它——这也是 `-ffast-math` 危险性的活例子。

动手：
- [ ] 给按列求和加 64×64 分块，看能挽回几倍
- [ ] 把 `std::list<int>` 全表求和 vs `std::vector<int>` 全表求和（同为 O(n)），测「链表税」有多重
- [ ] 用 `perf stat -d` 对比按行/按列的 cache-misses 数量，把现象和计数器对上

## 阶段 5：并行化（最后一步）

- 识别可并行的结构：map/filter/reduce 天然可分块；`std::reduce`（带执行策略需 TBB）或自己分块 + [线程池](thread-pool.md)（`thread_pool_demo` 第 1 节就是分块求和的样例）。
- Amdahl 定律：串行部分决定加速上限；先看热点是不是被锁/IO 串行化。
- 负载均衡：任务粒度太细则调度开销吃掉收益，太粗则核闲着；实践「块数 = 核数 × 2~4」起步。

动手：
- [ ] 把按行矩阵求和改成 4 线程分块版本，验证 4 核下接近线性加速（注意假共享！）
- [ ] 故意让块粒度变成 1 行/任务，观察调度开销如何吞掉收益

## 度量与工具

```bash
cmake --preset release && cmake --build --preset release   # 性能一律用 release
perf stat -d ./build/release/algorithm_optimization_demo    # IPC / cache-misses
perf record -g ./build/release/xxx && perf report           # 热点函数
```

- 每个实验：预热 → 计时 → **校验结果一致** → 报告加速比。
- 进阶：引入 Google Benchmark（FetchContent 同 GoogleTest），把 demo 的实验改写成正式 benchmark，跑 `--benchmark_repetitions=10` 看稳定性。

## 常见坑

- **用 Debug 数据做决策**（差距失真，且优化可能被 -O0 掩盖）。
- **优化了不是热点的代码**（97% 时间在 3% 代码里，先 profile）。
- **改完不验证正确性**（尤其浮点换序、并行化之后）。
- **基准数据太友好**（全 0 数组、有序输入会让你的分支预测/缓存结论失效；用随机+真实分布）。

## 延伸阅读

- 《算法导论》摊还分析章节；Skiena《算法设计手册》「战争故事」章节
- Chandler Carruth: *Tuning C++: Benchmarks, and CPUs, and Compilers! Oh My!*（CppCon）
- 《Performance Analysis and Tuning on Modern CPUs》（Denis Bakhvalov，免费的 red doubly linked list 电子版）
- errichto / Algorithms Live 关于 cache-friendly 算法的专题
