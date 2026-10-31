// serialization_benchmark：三种序列化的体积/耗时/可读性对照（中间件设计工程师核心科目）
//
//   方案 A：手写 POD + memcpy          —— 最快，但字段版本演进完全不兼容（反面教材）
//   方案 B：Protobuf（二进制+IDL）     —— 紧凑、跨语言、字段前后兼容；ROS2/gRPC 的基石
//   方案 C：nlohmann/json（文本）      —— 人可读、调试友好；体积 3-10 倍、解析慢一个量级
//
// 自测模式（默认）：断言三种方案 roundtrip 字段一致 + 打印对比表。
// 练习重点：
//   1. 为什么 protobuf 不传字段名只传编号（varint + wire format）；
//   2. 给 Frame 加一个字段，旧代码读新数据会怎样？（前向兼容的由来）
//   3. 把 robot_id 从 string 换成 enum+编号，看体积还能省多少（高基数字符串的代价）；
//   4. double 数组 wheel_speed 在 json 里多占多少——数值型遥测别用文本协议的原因。

#include "telemetry.pb.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// ---------- 方案 A：手写 POD（演示用途：真实系统禁止这样跨进程传结构体） ----------
struct PodFrame {
    std::uint64_t timestamp_ns;
    double x, y, theta;
    double wheel_speed[4];
    double voltage;
    std::uint32_t soc_percent;
    char robot_id[16];
};
static_assert(std::is_trivially_copyable_v<PodFrame>, "POD 帧必须可平凡拷贝");

struct Serialized {
    std::vector<unsigned char> bytes;
};

Serialized pod_serialize(const PodFrame& frame) {
    Serialized s;
    s.bytes.resize(sizeof(frame));
    std::memcpy(s.bytes.data(), &frame, sizeof(frame));
    return s;
}

PodFrame pod_deserialize(const Serialized& s) {
    PodFrame frame;
    std::memcpy(&frame, s.bytes.data(), sizeof(frame));
    return frame;
}

// ---------- 方案 B/C 公共：构造/校验同一帧数据 ----------
playground::telemetry::Frame make_frame() {
    playground::telemetry::Frame frame;
    frame.set_timestamp_ns(1761800000123456789ULL);
    frame.set_robot_id("robot-01");
    frame.mutable_pose()->set_x(1.25);
    frame.mutable_pose()->set_y(-3.5);
    frame.mutable_pose()->set_theta(0.70710678);
    for (double v : {0.9, 1.0, 1.0, 0.9}) frame.add_wheel_speed(v);
    frame.mutable_battery()->set_voltage(24.6);
    frame.mutable_battery()->set_soc_percent(87);
    return frame;
}

nlohmann::json frame_to_json(const playground::telemetry::Frame& frame) {
    nlohmann::json j;
    j["timestamp_ns"] = frame.timestamp_ns();
    j["robot_id"] = frame.robot_id();
    j["pose"] = {{"x", frame.pose().x()}, {"y", frame.pose().y()}, {"theta", frame.pose().theta()}};
    j["wheel_speed"] = frame.wheel_speed();
    j["battery"] = {{"voltage", frame.battery().voltage()},
                    {"soc_percent", frame.battery().soc_percent()}};
    return j;
}

// 统计时钟
template <typename F>
std::pair<std::chrono::nanoseconds, std::invoke_result_t<F>> timed(F&& f) {
    auto start = std::chrono::steady_clock::now();
    auto result = f();
    auto stop = std::chrono::steady_clock::now();
    return {stop - start, std::move(result)};
}

int run() {
    int failures = 0;
    auto expect = [&failures](bool cond, const std::string& what) {
        std::cout << (cond ? "[PASS] " : "[FAIL] ") << what << '\n';
        if (!cond) ++failures;
    };

    constexpr int kRounds = 20000;

    // ---------- A. POD ----------
    PodFrame pod{};
    pod.timestamp_ns = 1761800000123456789ULL;
    pod.x = 1.25; pod.y = -3.5; pod.theta = 0.70710678;
    pod.wheel_speed[0] = pod.wheel_speed[3] = 0.9;
    pod.wheel_speed[1] = pod.wheel_speed[2] = 1.0;
    pod.voltage = 24.6; pod.soc_percent = 87;
    std::strncpy(pod.robot_id, "robot-01", sizeof(pod.robot_id) - 1);

    auto [pod_time, pod_ser] = timed([&] { return pod_serialize(pod); });
    PodFrame pod_back = pod_deserialize(pod_ser);
    expect(pod_back.x == pod.x && pod_back.soc_percent == pod.soc_percent &&
               std::strncmp(pod_back.robot_id, "robot-01", 8) == 0,
           "POD memcpy roundtrip 字段一致");

    // ---------- B. Protobuf ----------
    auto frame = make_frame();
    auto [pb_time, pb_ser] = timed([&] {
        Serialized s;
        s.bytes.resize(frame.ByteSizeLong());
        frame.SerializeWithCachedSizesToArray(s.bytes.data());
        return s;
    });
    playground::telemetry::Frame pb_back;
    pb_back.ParseFromArray(pb_ser.bytes.data(), static_cast<int>(pb_ser.bytes.size()));
    expect(pb_back.pose().x() == frame.pose().x() && pb_back.robot_id() == frame.robot_id() &&
               pb_back.wheel_speed_size() == 4 && pb_back.battery().soc_percent() == 87,
           "Protobuf roundtrip 字段一致");

    // ---------- C. JSON ----------
    auto [json_time, json_str] = timed([&] { return frame_to_json(frame).dump(); });
    auto json_back = nlohmann::json::parse(json_str);
    expect(json_back["pose"]["x"] == frame.pose().x() &&
               json_back["wheel_speed"].size() == 4 &&
               json_back["battery"]["soc_percent"] == 87,
           "JSON roundtrip 字段一致");

    // ---------- 对比表 ----------
    // 单轮基准之外再给批量吞吐口径（含解析），更接近真实链路
    auto [pb_batch, ok_pb] = timed([&] {
        for (int i = 0; i < kRounds; ++i) {
            std::string s = frame.SerializeAsString();
            playground::telemetry::Frame back;
            back.ParseFromString(s);
        }
        return true;
    });
    auto [json_batch, ok_json] = timed([&] {
        for (int i = 0; i < kRounds; ++i) {
            std::string s = frame_to_json(frame).dump();
            auto back = nlohmann::json::parse(s);
        }
        return true;
    });

    std::cout << "\n=== 序列化对比（单帧体积 / " << kRounds << " 轮往返均值） ===\n";
    std::printf("%-12s %10zu bytes  %10s\n", "POD+memcpy", pod_ser.bytes.size(), "-");
    std::printf("%-12s %10zu bytes  %8.0f ns/轮\n", "Protobuf", pb_ser.bytes.size(),
                pb_batch.count() / double(kRounds));
    std::printf("%-12s %10zu bytes  %8.0f ns/轮\n", "JSON", json_str.size(),
                json_batch.count() / double(kRounds));
    std::cout << "（POD 单次序列化耗时 " << pod_time.count() << " ns，仅供参考）\n";
    std::cout << "结论：体积 JSON ≈ " << std::setprecision(2)
              << double(json_str.size()) / pb_ser.bytes.size() << "x Protobuf；"
              << "耗时 JSON ≈ " << json_batch.count() / double(pb_batch.count())
              << "x Protobuf\n";

    return failures == 0 ? 0 : 1;
}

}  // namespace

int main() { return run(); }
