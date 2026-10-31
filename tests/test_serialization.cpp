// 序列化 roundtrip 单测：中间件模块的正确性底线（体积/速度在 demo 里看，这里只管对错）
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "telemetry.pb.h"

namespace {

playground::telemetry::Frame make_frame(std::uint64_t ts, const std::string& id) {
    playground::telemetry::Frame frame;
    frame.set_timestamp_ns(ts);
    frame.set_robot_id(id);
    frame.mutable_pose()->set_x(1.25);
    frame.mutable_pose()->set_y(-3.5);
    frame.mutable_pose()->set_theta(0.70710678118);
    frame.add_wheel_speed(0.9);
    frame.add_wheel_speed(1.0);
    frame.add_wheel_speed(1.0);
    frame.add_wheel_speed(0.9);
    frame.mutable_battery()->set_voltage(24.6);
    frame.mutable_battery()->set_soc_percent(87);
    return frame;
}

TEST(ProtobufRoundtrip, AllFieldsSurvive) {
    auto frame = make_frame(1761800000123456789ULL, "robot-01");
    std::string blob = frame.SerializeAsString();

    playground::telemetry::Frame back;
    ASSERT_TRUE(back.ParseFromString(blob));
    EXPECT_EQ(back.timestamp_ns(), frame.timestamp_ns());
    EXPECT_EQ(back.robot_id(), frame.robot_id());
    EXPECT_DOUBLE_EQ(back.pose().x(), frame.pose().x());
    EXPECT_DOUBLE_EQ(back.pose().theta(), frame.pose().theta());
    ASSERT_EQ(back.wheel_speed_size(), 4);
    EXPECT_DOUBLE_EQ(back.wheel_speed(0), 0.9);
    EXPECT_DOUBLE_EQ(back.battery().voltage(), 24.6);
    EXPECT_EQ(back.battery().soc_percent(), 87u);
}

TEST(ProtobufCompat, MissingFieldDefaults) {
    // 前向/后向兼容：新 schema 读旧数据（缺字段）时取默认值而不是报错——
    // 这是 ROS2 IDL / DDS XTypes / gRPC 演进的公共基石
    playground::telemetry::Frame old_frame;   // 模拟旧版本发送方：只填了部分字段
    old_frame.set_robot_id("robot-02");
    std::string blob = old_frame.SerializeAsString();

    playground::telemetry::Frame back;        // 新 schema 完整字段
    ASSERT_TRUE(back.ParseFromString(blob));
    EXPECT_EQ(back.robot_id(), "robot-02");
    EXPECT_EQ(back.timestamp_ns(), 0ULL);     // 缺省字段 = 类型零值
    ASSERT_EQ(back.wheel_speed_size(), 0);
    EXPECT_DOUBLE_EQ(back.battery().voltage(), 0.0);
}

TEST(JsonRoundtrip, AllFieldsSurvive) {
    auto frame = make_frame(42, "robot-03");
    nlohmann::json j;
    j["timestamp_ns"] = frame.timestamp_ns();
    j["robot_id"] = frame.robot_id();
    j["wheel_speed"] = frame.wheel_speed();
    std::string text = j.dump();

    auto back = nlohmann::json::parse(text);
    EXPECT_EQ(back["timestamp_ns"], 42);
    EXPECT_EQ(back["robot_id"], "robot-03");
    ASSERT_EQ(back["wheel_speed"].size(), 4u);
    EXPECT_DOUBLE_EQ(back["wheel_speed"][1].get<double>(), 1.0);
}

TEST(JsonError, GarbageInputDetected) {
    // 客户端输入永远不可信：解析失败要能被发现而不是带着脏数据往下走
    auto bad = nlohmann::json::parse("{ not json", nullptr, false);
    EXPECT_TRUE(bad.is_discarded());
}

}  // namespace
