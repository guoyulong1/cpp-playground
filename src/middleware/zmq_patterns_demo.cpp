// zmq_patterns_demo：ZeroMQ 三种核心消息模式实操（中间件设计工程师）
//
//   1. REQ/REP      同步问答（RPC 的雏形）——锁步语义：必须一问一答交替
//   2. PUB/SUB      广播订阅（ telemetry 分发的经典拓扑）——晚加入的订阅者收不到历史
//   3. PUSH/PULL    任务扇出/汇入（流水线负载均衡）——负载自动路由给空闲 worker
//
// 全部走 inproc（进程内 IPC，不走 TCP），单进程自包含、CI 可跑。
// 练习重点：
//   1. zmq_socket 的「连接是异步的」——bind/connect 顺序无关，消息在连接建立前发出会怎样？
//   2. PUB/SUB 的慢消费者默认丢消息（conflate/linger/HWM 各自管什么）——对照 ROS2 QoS
//      的 BestEffort+KeepLast(depth=1)：传感器流允许丢、指令流必须 Reliable；
//   3. 把 inproc 换成 tcp://，跨进程跑两个终端，体会「传输层与模式正交」；
//   4. 对照手写 epoll echo_server：ZMQ 把「连接管理/重连/背压」全做了，代价是什么？
//      （更多内存、一跳用户态队列——这也是 DDS 与「裸 TCP」的差异缩影）

#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void expect(bool cond, const std::string& what) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << what << '\n';
    if (!cond) ++failures;
}

// ---------- 1. REQ/REP：一问一答 ----------
void demo_req_rep(zmq::context_t& ctx) {
    std::cout << "\n--- REQ/REP 同步问答 ---\n";
    zmq::socket_t server(ctx, zmq::socket_type::rep);
    server.bind("inproc://reqrep");

    // 线程扮演客户端：连发两问（REQ 的锁步规则：一问必须等一答）
    std::thread client([&ctx] {
        zmq::socket_t sock(ctx, zmq::socket_type::req);
        sock.connect("inproc://reqrep");
        for (int i = 1; i <= 2; ++i) {
            std::string q = "ping-" + std::to_string(i);
            sock.send(zmq::buffer(q), zmq::send_flags::none);
            zmq::message_t reply;
            (void)sock.recv(reply);  // 必须先收到答案才能发下一问
        }
    });

    int answered = 0;
    while (answered < 2) {
        zmq::message_t req;
        (void)server.recv(req);
        std::string q(static_cast<char*>(req.data()), req.size());
        server.send(zmq::buffer("pong@" + q), zmq::send_flags::none);
        ++answered;
    }
    client.join();
    expect(answered == 2, "REQ/REP 完成两轮问答（锁步语义验证）");
}

// ---------- 2. PUB/SUB：广播订阅 ----------
void demo_pub_sub(zmq::context_t& ctx) {
    std::cout << "\n--- PUB/SUB 广播订阅 ---\n";
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.bind("inproc://pubsub");

    constexpr int kMessages = 100;
    std::atomic<int> received{0};

    std::thread sub([&ctx, &received] {
        zmq::socket_t sock(ctx, zmq::socket_type::sub);
        sock.connect("inproc://pubsub");
        sock.set(zmq::sockopt::subscribe, "sensor");  // 只订 sensor 前缀
        // 订阅是异步传播的（连接+订阅两条命令），期间 PUB 发出的消息直接丢弃——
        // 这不是 bug，是 PUB/SUB 的「允许丢」语义（对照 ROS2 sensor QoS 的 BestEffort）。
        // 退出策略：收满 or 空闲 500ms，绝不无条件等待。
        int idle_ms = 0;
        zmq::message_t msg;
        while (received.load() < kMessages && idle_ms < 500) {
            if (sock.recv(msg, zmq::recv_flags::dontwait).has_value()) {
                ++received;
                idle_ms = 0;
            } else {
                ++idle_ms;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    // 等订阅传播完成再开闸：把「订阅建立前丢消息」的窗口尽量关小（但语义上不保证清零）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (int i = 0; i < kMessages; ++i) {
        pub.send(zmq::buffer("sensor " + std::to_string(i)), zmq::send_flags::none);
    }
    sub.join();
    // 允许少量丢失（订阅传播竞态），但绝大部分必须到达——否则说明 topic 过滤没生效
    expect(received.load() >= kMessages - 10,
           "PUB/SUB 收到 " + std::to_string(received.load()) + "/" +
               std::to_string(kMessages) + " 条（topic 过滤生效，允许订阅竞态丢少量）");
}

// ---------- 3. PUSH/PULL：任务扇出 ----------
void demo_push_pull(zmq::context_t& ctx) {
    std::cout << "\n--- PUSH/PULL 任务扇出（流水线负载均衡） ---\n";
    // 拓扑：PUSH（ventilator，bind）→ N 个 PULL worker（connect）。
    // PUSH 在发送侧，PULL 只能收——socket 类型决定可执行的操作（方向反了就是 ENOTSUP）。
    zmq::socket_t ventilator(ctx, zmq::socket_type::push);
    ventilator.bind("inproc://sink");

    constexpr int kWorkers = 3;
    constexpr int kTasks = 90;
    std::atomic<int> done{0};

    // 三个 PULL worker，谁空闲谁领任务（ZMQ 的负载均衡语义）。
    // 退出条件用「任务总数达成 + 一段静默期」而不是毒丸：
    // PUSH 的公平队列不保证 END 均摊给每个 worker，毒丸会引入不确定性。
    std::vector<std::thread> workers;
    for (int w = 0; w < kWorkers; ++w) {
        workers.emplace_back([&ctx, &done] {
            zmq::socket_t sock(ctx, zmq::socket_type::pull);
            sock.connect("inproc://sink");
            int idle_ms = 0;
            while (true) {
                zmq::message_t msg;
                if (sock.recv(msg, zmq::recv_flags::dontwait).has_value()) {
                    done.fetch_add(1);
                    idle_ms = 0;
                    continue;
                }
                // 任务全做完且连续 300ms 无新消息 → 安全退出
                if (done.load() >= kTasks) {
                    if (++idle_ms >= 300) break;
                } else {
                    idle_ms = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 等 worker connect
    for (int i = 0; i < kTasks; ++i) {
        ventilator.send(zmq::buffer("task-" + std::to_string(i)), zmq::send_flags::none);
    }

    for (auto& t : workers) t.join();
    expect(done.load() >= kTasks,
           "PUSH/PULL 三个 worker 分摊 " + std::to_string(kTasks) + " 个任务并全部完成（实际 " +
               std::to_string(done.load()) + "）");
}

}  // namespace

int main() {
    zmq::context_t ctx;
    demo_req_rep(ctx);
    demo_pub_sub(ctx);
    demo_push_pull(ctx);
    std::cout << (failures == 0 ? "\n自测全部通过\n" : "\n存在失败项\n");
    return failures == 0 ? 0 : 1;
}
