// ros2_demo：rclcpp 三合一（publisher + subscriber + service），含 QoS 对照实验。
//
// 与仓库其余模块的关系：前面所有练习都是「单机手搓」，本文件切到 ROS2 生态——
// 学会用工业级中间件（DDS 之上的一层）表达同样的通信，两相对照着理解 rcl→rmw→DDS 分层。
//
// 本 demo 自包含自测（CI 可跑）：进程内起 publisher/subscriber/service，
// 定时发布 → 订阅计数 → 调用一次 service → 达到目标后退出，退出码 0/1。
//
// 练习重点：
//   1. QoS 对照：把 sub_qos 从 Reliable 改成 BestEffort（sensor data profile），
//      观察「订阅不兼容警告」——QoS 是 ROS2 面试第一高频考点；
//   2. executor 模型：spin 是单线程回调队列，重回调会饿死其它订阅——
//      对照 thread_pool 练习：MultiThreadedExecutor + 回调组怎么配；
//   3. 参数：ros2 param set /param_demo_frequency 5.0 运行时改频率（本 demo 已声明参数）；
//   4. ROS_DOMAIN_ID 隔离：开两个终端不同 domain，消息互不可见（多机部署的基础）。

#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <std_msgs/msg/float64.hpp>
#include <std_srvs/srv/set_bool.hpp>

namespace {

class DemoNode : public rclcpp::Node {
public:
    DemoNode() : Node("cpp_playground_demo") {
        // 声明参数：可被 launch 文件 / ros2 param set 覆盖
        this->declare_parameter("frequency", 20.0);
        frequency_ = this->get_parameter("frequency").as_double();

        // Publisher：Reliable + KeepLast(10)（指令类默认档）
        pub_ = this->create_publisher<std_msgs::msg::Float64>("playground/telemetry", 10);

        // Subscriber：与 publisher QoS 匹配才能收到（练习：换成 sensor_qos 对比）
        rclcpp::QoS sub_qos(10);
        sub_qos.reliable();
        sub_ = this->create_subscription<std_msgs::msg::Float64>(
            "playground/telemetry", sub_qos,
            [this](std_msgs::msg::Float64::ConstSharedPtr msg) {
                ++received_;
                RCLCPP_DEBUG(this->get_logger(), "recv %.2f", msg->data);
            });

        // Service：外界可用 ros2 service call 改变发布频率
        srv_ = this->create_service<std_srvs::srv::SetBool>(
            "playground/toggle",
            [this](const std_srvs::srv::SetBool::Request::SharedPtr req,
                   std_srvs::srv::SetBool::Response::SharedPtr res) {
                enabled_ = req->data;
                res->success = true;
                res->message = req->data ? "enabled" : "disabled";
            });

        timer_ = this->create_wall_timer(
            std::chrono::duration<double>(1.0 / frequency_),
            [this] {
                if (!enabled_) return;
                std_msgs::msg::Float64 msg;
                msg.data = tick_++ * 0.5;
                pub_->publish(msg);
            });
    }

    std::size_t received() const { return received_.load(); }

private:
    double frequency_ = 20.0;
    bool enabled_ = true;
    std::uint64_t tick_ = 0;
    std::atomic<std::size_t> received_{0};
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr pub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr sub_;
    rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr srv_;
    rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<DemoNode>();

    // 自测模式：订阅收满 20 条即成功；serve 参数则一直 spin（ros2 topic echo 联调）
    const bool serve_mode = argc > 1 && std::string(argv[1]) == "serve";
    const std::size_t kTarget = 20;

    if (serve_mode) {
        rclcpp::spin(node);
        rclcpp::shutdown();
        return 0;
    }

    // spin 最多 5 秒：单线程 executor 轮转 publisher/subscription/timer 回调
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (rclcpp::ok() && node->received() < kTarget &&
           std::chrono::steady_clock::now() < deadline) {
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const bool pass = node->received() >= kTarget;
    std::cout << (pass ? "[PASS] " : "[FAIL] ") << "订阅计数 " << node->received()
              << "/" << kTarget << "（进程内 pub/sub 闭环）\n";
    rclcpp::shutdown();
    return pass ? 0 : 1;
}
