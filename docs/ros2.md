# ROS2 学习方案（`src/ros2/`）

对应岗位：**ROS2 开发工程师**（机器人软件开发，国内均薪 18.8K，资深 25K+）。

本仓库的定位：把 rclcpp 当普通库用的「单文件起步」——不用 colcon/ament 工作区，
和其它模块一样 `cmake --build` 直接跑，先把**通信模型本身**搞透；标准工程形态（ament 包）是第二阶段。

## 0. 跑起来

```bash
source /opt/ros/humble/setup.bash   # rclcpp 检测由 preset 的 CMAKE_PREFIX_PATH 完成
./build/cn/ros2_demo                # 自测模式：进程内 pub→sub 收满 20 条退出
./build/cn/ros2_demo serve          # 联调模式：配合下面的 CLI 观察
```

联调三件套（另开终端）：

```bash
ros2 topic list                       # 能看到 playground/telemetry
ros2 topic echo /playground/telemetry
ros2 service call /playground/toggle std_srvs/srv/SetBool "{data: false}"
ros2 param set /cpp_playground_demo frequency 5.0
```

## 1. 通信模型：五件套（本 demo 覆盖 pub/sub/service/param）

| 概念 | demo 里的位置 | 关键理解 |
|---|---|---|
| Topic（pub/sub） | `create_publisher` / `create_subscription` | 异步流；QoS 决定匹配与可靠性 |
| Service（req/resp） | `create_service<SetBool>` | 同步问答；对照 ZMQ REQ/REP 的锁步 |
| Parameter | `declare_parameter("frequency")` | 运行时可改；launch 文件可注入 |
| Executor | `spin_some` 循环 | 单线程回调队列——顺序执行是特性也是坑 |
| QoS | `sub_qos.reliable()` | **面试第一高频**，见下 |

**QoS 对照实验**（改一行代码看现象）：
- pub 用默认（Reliable），sub 改 `best_effort()` → 启动即打「incompatible QoS」警告，一条都收不到；
- 换回匹配 → 恢复。结论：**QoS 匹配是订阅建立的前置条件**，与 ZMQ PUB/SUB 的「允许丢」是两个层级的语义。

## 2. 演进路线（本模块的练习阶梯）

- [ ] L1：sub 改 BestEffort，观察 incompatible 警告（上面实验）
- [ ] L1：加 Action（`rclcpp_action`，用 `example_interfaces/Fibonacci`）——五件套补全
- [ ] L1：`ros2 param set` 动态调频，验证 timer 周期变化
- [ ] L2：`MultiThreadedExecutor` + `CallbackGroup`（MutuallyExclusive/Reentrant），重回调不再饿死订阅
- [ ] L2：组件化（`rclcpp_components::Node`）+ 同进程 intra-process 零拷贝
- [ ] L2：生命周期节点（LifecycleNode，configure→activate 状态机）
- [ ] L3：升级为标准 ament 包：`package.xml` + `ament_cmake` + colcon 构建，对照 CMake 直用的差异
- [ ] L3：自定义 `.msg`（rosidl）→ 与 `src/middleware/telemetry.proto` 对照 IDL 同构性
- [ ] L3：tf2 静态/动态变换 + `tf2_tools view_frames`

## 3. 与本机环境联动（Humble 全家桶已装）

- **Nav2**：`ros2 launch nav2_bringup navigation_launch.py`（需要地图与仿真）
- **Gazebo**：`ros2 launch ros_gz_sim ...`（Ignition Fortress 已装）
- **RMW 切换**：`export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` 前后跑 `ros2 topic hz`
- **MoveIt / ros2_control / rosbag2 / ros2_tracing**：全部就绪，进阶练习见
  `~/ZCodeProject/capability-plan/index.html` 第 06 章 Q1 路线

## 4. 资源

- 官方 Tutorials（docs.ros.org/en/humble）——概念与 CLI 必读
- ros2_control 官方教程（control.ros.org）——硬件接口编写
- Nav2 文档（navigation.ros.org）——行为树与调参
- 鱼香 ROS 社区（fishros.com）——中文生态
