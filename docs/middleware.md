# 中间件设计学习方案（`src/middleware/`）

对应岗位：**中间件设计工程师**（机器人/分布式方向，专家岗 50-80K）。仓库里用两个最小可跑的 demo 承载两大核心科目：**序列化**与**消息模式**，路线从手感到原理再到工业生态。

## 0. 为什么是这两个切入点

中间件的日常工作 = 「把数据可靠/高效地从 A 搬到 B」：

- **怎么表示数据** → 序列化（IDL/编码/兼容性）
- **怎么搬** → 消息模式（拓扑/可靠性/背压）

DDS、Kafka、gRPC、ROS2 的一切高级特性，都是这两件事的变体。

## 1. 序列化：`serialization_benchmark`

三向对比（同一帧机器人遥测数据）：

| 方案 | 体积（实测） | 耗时（实测） | 关键性质 |
|---|---|---|---|
| POD + memcpy | 96 B | ~2.3 μs/次 | 最快；字段演进完全不兼容（跨进程传 POD 是反面教材） |
| Protobuf | 96 B | ~831 ns/轮 | 紧凑；字段编号 + wire format，前向/后向兼容 |
| JSON | 180 B（1.9x） | ~43 μs/轮（52x） | 人可读；调试友好，生产遥测别用 |

阅读顺序：
1. `src/middleware/proto/telemetry.proto` —— 字段编号、repeated、嵌套 message；
2. `serialization_benchmark.cpp` —— `SerializeWithCachedSizesToArray` / `ParseFromArray`；
3. `tests/test_serialization.cpp` —— `MissingFieldDefaults` 用例演示兼容性。

**阶段练习**：
- [ ] L1：给 Frame 加 `string firmware` 字段，用旧二进制喂新代码，验证不报错
- [ ] L1：把 wheel_speed 从 double 改 float，看体积变化
- [ ] L2：手写 varint 编解码（读 protobuf wire format 文档），理解「不用字段名只用编号」
- [ ] L2：接入 FlatBuffers/Cap'n Proto（零拷贝反序列化），对比 API 形态
- [ ] L3：阅读 rosidl 生成的 `telemetry.pb.h` 对应的 ROS2 msg 类型支持代码（`/opt/ros/humble/include`），理解 .msg → IDL → DDS type support 链路

## 2. 消息模式：`zmq_patterns_demo`（ZeroMQ）

三种模式一次跑全（inproc，自测断言）：

| 模式 | 语义 | 工业对应 |
|---|---|---|
| REQ/REP | 一问一答锁步 | RPC（gRPC unary 的雏形） |
| PUB/SUB | 广播；**晚加入收不到历史、订阅建立竞态会丢** | telemetry 分发；对照 ROS2 sensor QoS（BestEffort） |
| PUSH/PULL | 扇出负载均衡，谁空闲谁领 | 任务队列；Kafka consumer group 的同构 |

demo 里埋了三个真实踩坑点（注释里都有展开）：
1. PUB/SUB 订阅传播是**异步**的——开闸前必须等，但语义上**永远允许丢**；
2. PUSH 的公平队列**不保证**毒丸均摊——退出用「总数达成 + 静默期」；
3. socket 类型决定可执行操作——方向接反就是 `ENOTSUP`。

**阶段练习**：
- [ ] L1：把 inproc 换 `tcp://*:5555`，两个终端跨进程跑（传输层与模式正交）
- [ ] L1：PUB 设 `conflate=1`（只保留最后一条），对照 ROS2 `KeepLast(depth=1)`
- [ ] L2：用仓库 ThreadPool 替换 PUSH/PULL 的 worker，对比「线程内队列 vs 进程间队列」
- [ ] L2：REQ/REP 改 ROUTER/DEALER（无锁步、多对一），实现一个简易 broker
- [ ] L3：libzmq 源码 `src/ipc_address.cpp`/pipe 语义；对照 DDS 的 RTPS reader/writer 匹配

## 3. 与本机环境的衔接（强烈建议实操）

- **Kafka**（`~/dev-stack` 已起）：用 pip 的 kafka-python 或 Go 写生产者，把 telemetry.pb 灌进 `demo` topic；`docker exec dev-kafka /opt/kafka/bin/kafka-console-consumer.sh --bootstrap-server localhost:9092 --topic demo --from-beginning` 消费验证；
- **RMW 对比**：`export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` 前后跑 `ros2 topic hz`，体会 DDS 实现差异；
- **Wireshark**：抓 UDP 7400~ 端口看 RTPS SPDP 发现报文（中间件面试的硬通货）。

## 4. 书目与资源

- ZeroMQ 官方 Guide（zguide.zeromq.org，免费）——模式圣经
- Protobuf 官方 Encoding 文档——varint/wire format
- 《Kafka 权威指南》——分区/副本/语义
- Fast DDS / Cyclone DDS 官方文档（QoS、发现、共享内存）
- Raft 论文（raft.github.org）——共识入门必读
