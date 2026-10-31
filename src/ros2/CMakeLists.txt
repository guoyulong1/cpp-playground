# ROS2 模块：依赖 rclcpp（顶层已 QUIET 检测）。
# 注意：与 ament/colcon 工作区无关——本仓库把 rclcpp 当普通库用，保持练手场形态；
# 正式的 ROS2 包组织（ament_cmake + package.xml + colcon）见 docs/ros2.md 的演进路线。
add_executable(ros2_demo ros2_demo.cpp)
target_link_libraries(ros2_demo PRIVATE
    rclcpp::rclcpp
    std_msgs::std_msgs
    std_srvs::std_srvs)
target_compile_features(ros2_demo PRIVATE cxx_std_17)
