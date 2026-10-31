# cpp-playground

![CI](https://github.com/guoyulong1/cpp-playground/actions/workflows/ci.yml/badge.svg)

C++ 练手场 → **四岗位能力实战仓库**：每个主题都有最小可跑的骨架 + 一份详细学习方案（见 `docs/`），
并用高 star 工业级开源库（Protobuf / ZeroMQ / nlohmann-json / cpp-httplib / spdlog）对标真实技术栈。

## 四岗位能力映射

| 岗位方向 | 仓库模块 | 对标技术 |
|---|---|---|
| 💻 软件开发工程师 | `src/process|memory|thread|algo/`、工程设施 | fork/exec、RAII、线程池、epoll、CMake/CI |
| 🔧 中间件设计工程师 | `src/middleware/` | 序列化（Protobuf vs JSON）、消息模式（ZeroMQ）、（进阶：DDS/QoS） |
| 🤖 ROS2 开发工程师 | `src/ros2/`（条件编译） | rclcpp、pub/sub/service、QoS、executor |
| 🚀 系统部署工程师 | `src/network/http_server.cpp` + `deploy/` | REST + `/metrics`、Docker、systemd、Ansible、CI/CD |

> 配套的《四岗位能力体系建设方案》（能力矩阵 / 12 个月路线 / 毕业项目）见本机 `~/ZCodeProject/capability-plan/index.html`。

## 主题一览

| 主题 | 位置 | 内容 |
|---|---|---|
| 进程管理 | `src/process/` | fork/exec/wait 闭环、多 worker 进程池（管道通信） |
| 内存管理 | `src/memory/`、`include/playground/` | RAII、智能指针、固定块内存池、内存优化实验 |
| 算法改良 | `src/algo/` | 复杂度、扩容成本、访存模式三组对照实验 |
| 线程池 | `src/thread/`、`include/playground/` | mutex+cv 线程池，future 传值传异常（带单测） |
| 网络编程 | `src/network/` | epoll LT echo server（手写）；**http_server（cpp-httplib + `/metrics`）** |
| **中间件** | `src/middleware/` | **序列化三向对比（POD/Protobuf/JSON）、ZeroMQ 三模式（REQ-REP/PUB-SUB/PUSH-PULL）** |
| **ROS2** | `src/ros2/` | **rclcpp pub/sub/service 三合一 + QoS（检测到 rclcpp 自动启用）** |
| **可观测基建** | `include/playground/logger.hpp`、`metrics.hpp` | **spdlog 双 sink 日志、手写 Prometheus counter/gauge 文本协议** |
| **部署** | `deploy/` | **多阶段 Dockerfile、compose（接入 Prometheus 抓取）、systemd、Ansible** |
| clang-tidy | `.clang-tidy` | 本地与 CI 共用一套规则 |
| Code Review | `.github/workflows/code-review.yml` | 阿里开源 [Open Code Review](https://github.com/alibaba/open-code-review) |
| CI/CD | `.github/workflows/ci.yml`、`release.yml` | 双编译器 + clang-tidy + ASan/UBSan + ROS2 容器 job；tag 出 Releases + GHCR 镜像 |

## 快速开始

需要 CMake ≥ 3.21、Ninja、GCC ≥ 11 或 Clang ≥ 14、protobuf-compiler + libprotobuf-dev：

```bash
sudo apt install build-essential cmake ninja-build protobuf-compiler libprotobuf-dev
```

```bash
# 境外网络（CI 等）：依赖 configure 时自动 FetchContent 下载
cmake --preset default && cmake --build --preset default
ctest --preset default

# 国内被墙网络：先预热依赖到 ~/.cache（走镜像加速），再用 cn preset（完全离线）
./scripts/fetch-deps-cn.sh
cmake --preset cn && cmake --build --preset cn
```

跑 demo（自测模式，CI 可跑）：

```bash
./build/default/raii_demo                     # RAII / 智能指针
./build/default/fork_exec_demo                # fork → exec → waitpid
./build/default/process_pool                  # 进程池 + 管道
./build/default/memory_optimization_demo      # 内存优化实验（release 构建看数量级）
./build/default/algorithm_optimization_demo   # 算法对照实验
./build/default/thread_pool_demo              # 线程池
./build/default/echo_server                   # epoll 回显
./build/default/http_server                   # REST + /metrics（可观测服务）
./build/default/serialization_benchmark       # POD vs Protobuf vs JSON
./build/default/zmq_patterns_demo             # ZeroMQ 三模式

source /opt/ros/humble/setup.bash && ./build/default/ros2_demo   # ROS2（装有 Humble 时）
```

前台服务模式：

```bash
./build/default/http_server serve 18080
curl localhost:18080/healthz && curl localhost:18080/metrics
```

其他 preset：`release`、`asan`（AddressSanitizer + UBSan）、`cn`（国内网络离线依赖）。

## 部署实验（`deploy/`）

```bash
# 容器化：多阶段构建 + 非 root + healthcheck
sg docker -c "docker build -f deploy/Dockerfile -t cpp-playground:http-demo ."
sg docker -c "docker run -p 18080:18080 cpp-playground:http-demo"

# 接入本机 dev-stack 监控栈（~/dev-stack 需已 up）：/metrics 被 Prometheus 抓取
sg docker -c "docker compose -f deploy/docker-compose.yml up -d"
# Grafana（localhost:3000）→ Explore → playground_http_requests_total

# systemd / Ansible：见 deploy/systemd/ 与 deploy/ansible/
```

## 仓库结构

```
.
├── CMakeLists.txt / CMakePresets.json   # default / release / asan / cn 四套 preset
├── cmake/ThirdParty.cmake               # FetchContent 依赖（fmt/spdlog/json/httplib/libzmq+cppzmq）
├── scripts/fetch-deps-cn.sh             # 被墙网络依赖预热（走镜像加速）
├── docs/                                # 每主题一份详细学习方案
│   ├── middleware.md  ros2.md  deployment.md  （memory/thread/network/algorithm 同前）
├── include/playground/
│   ├── memory_pool.hpp  thread_pool.hpp
│   ├── logger.hpp                       # spdlog 封装（双 sink）
│   └── metrics.hpp                      # 手写 Prometheus 指标
├── src/
│   ├── process/ memory/ algo/ thread/ network/（+http_server.cpp）
│   ├── middleware/（serialization_benchmark / zmq_patterns_demo / proto/）
│   ├── ros2/（条件编译，检测到 rclcpp 才启用）
│   └── support/logger.cpp
├── tests/（memory_pool / thread_pool / serialization 三组单测）
├── deploy/（Dockerfile / compose / systemd / ansible）
└── .github/workflows/（ci / code-review / release）
```

## 练习主题与 TODO

### 1. 进程管理（`src/process/`）

- `fork_exec_demo.cpp`：fork 出子进程 → exec 成 `/bin/echo` → waitpid 回收并解析退出状态。
- `process_pool.cpp`：父进程 fork 出 N 个 worker，每个 worker 一条任务管道，结果经共享管道写回。

- [ ] 给 `process_pool` 加 SIGINT 优雅退出（注册信号处理，先停派发再回收）
- [ ] 用 `waitpid(WNOHANG)` 演示僵尸进程的产生与消灭
- [ ] 把「轮流派发」改成 epoll 事件驱动
- [ ] 实现一个 mini shell：解析命令、支持 `|` 管道与 `>` 重定向

### 2. 内存管理（`include/playground/` + `src/memory/`）

- `MemoryPool`：一次性向系统申请大块内存，内部用空闲链表分配/回收。
- `raii_demo`：手写 RAII 守卫、`unique_ptr` 自定义 deleter、`shared_ptr` 引用计数、`weak_ptr`。
- `memory_optimization_demo`：结构体布局对齐、vector reserve、SSO、池化 vs 全局 new 四个实验。

- [ ] `allocate()` 池满时改为抛 `std::bad_alloc`，或实现自动扩容
- [ ] `deallocate()` 增加越界 / 重复释放检测
- [ ] 写一个 `shared_ptr` 环形引用案例，再用 `weak_ptr` 修复
- [ ] 故意写一个 use-after-free，用 `cmake --preset asan` 体验 ASan 报告
- [ ] 按行求和改分块（tiling），看能挽回几倍性能
- [ ] 用 `std::pmr::monotonic_buffer_resource` 把 vector 分配次数压到 0
- [ ] 用 heaptrack 剖析任意 demo 的堆分配来源

### 3. 线程池（`src/thread/`）

- [ ] 加 `stop_now()`：立即停止不排干，配套单测
- [ ] 加 `wait_all()`：原子计数 + 唤醒
- [ ] CMakePresets 加 tsan preset，查数据竞争
- [ ] 压测不同线程数吞吐，画曲线找最优 worker 数

### 4. 算法改良（`src/algo/`）

- [ ] 加第三种两数之和：排序 + 双指针
- [ ] 按列求和加 64×64 分块
- [ ] `std::list` vs `std::vector` 全表求和，量化「链表税」
- [ ] `perf stat -d` 把按行/按列的 cache-misses 对上

### 5. 网络编程（`src/network/`）

- `echo_server`：epoll LT 单线程 Reactor（自测模式 + serve 前台）。
- `http_server`：cpp-httplib REST（`/healthz` `/api/echo` `/api/stats` `/metrics`），自测 5 项断言。

- [ ] echo_server 订阅 `EPOLLRDHUP`；改造 ET 模式
- [ ] echo_server 引入输入缓冲解决粘包，实现按行回显
- [ ] **用 wrk 压测 http_server，记录 QPS（`sudo apt install wrk`）**
- [ ] **把 logger 的 pattern 改成 JSON，接入 Loki（Grafana 查日志）**
- [ ] **metrics.hpp 加 Histogram 类型（bucket 分位数是 P99 指标的来源）**

### 6. 中间件（`src/middleware/`）→ 方案见 [docs/middleware.md](docs/middleware.md)

- `serialization_benchmark`：POD/Protobuf/JSON 三向对比（自测含 roundtrip 断言）。
- `zmq_patterns_demo`：REQ/REP、PUB/SUB、PUSH/PULL 三模式（inproc，CI 可跑）。

- [ ] 给 telemetry.proto 加字段，验证前后兼容（旧数据新代码）
- [ ] 把 ZMQ demo 的 inproc 换成 tcp://，跨终端跑
- [ ] 对照本机 dev-stack 的 Kafka：用 cppkafka/conda 生产者把 telemetry 写进 topic
- [ ] **进阶**：阅读 libzmq 的 HWM/linger 语义，写实验对照 ROS2 QoS 的 KeepLast/Reliable
- [ ] **进阶**：Cyclone DDS 源码（本机 ros-humble-rmw-cyclonedds-cpp）读发现协议

### 7. ROS2（`src/ros2/`，检测到 rclcpp 自动启用）→ 方案见 [docs/ros2.md](docs/ros2.md)

- `ros2_demo`：pub/sub/service 三合一 + 参数 + QoS（自测 20 条闭环断言）。

- [ ] 把 sub QoS 改 BestEffort，观察 incompatible QoS 警告
- [ ] MultiThreadedExecutor + 回调组，重回调不再饿死其它订阅
- [ ] 升级为标准 ament 包（package.xml + colcon），对照两种工程组织
- [ ] 自定义 msg（需 rosidl 包结构）→ 体会 IDL 与 telemetry.proto 的同构性

### 8. 部署（`deploy/` + workflows）→ 方案见 [docs/deployment.md](docs/deployment.md)

- [ ] compose 里加 Prometheus 抓取 job，Grafana 画出 QPS 大盘
- [ ] Dockerfile 换 distroless 基础镜像，对比体积与调试体验
- [ ] ansible 剧本跑 `--check --diff` 干跑，再实际部署到 systemd
- [ ] 打 tag v0.2.0 触发 release.yml，验证 Releases 产物与 GHCR 镜像
- [ ] 给 ci.yml 加 macOS 矩阵、FetchContent 缓存（actions/cache）

### 9. clang-tidy 与 AI Code Review

```bash
clang-tidy -p build/default src/network/http_server.cpp
```

- [ ] 注释掉 `.clang-tidy` 某条 `-xxx`，看多报了什么
- [ ] ci.yml 的 clang-tidy 步骤改成「有 warning 就失败」
- [ ] 配好 OCR secrets 提 PR 看 AI 评审

## 推荐练习工作流

每个练习走完整闭环：开分支 → 提 PR → 看 CI 与 AI 评审 → 改绿 → 合并。

```bash
git switch -c feat/middleware-histogram
# ...改代码、本地 ctest --preset default...
git push -u origin feat/middleware-histogram
gh pr create --fill
```
