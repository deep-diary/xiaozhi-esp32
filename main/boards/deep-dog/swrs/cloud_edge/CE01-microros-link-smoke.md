# CE01 · micro-ROS 链路冒烟（阶段 A）

| 项 | 内容 |
|----|------|
| ID | CE01 |
| 映射 | a3_arm_ws **E1**、**E9**、**E4（发布侧最小）** |
| 依赖 | WiFi STA（N02）、同网段可达的 micro-ROS Agent |
| 代码 | [`microros/`](../../microros/) · [`esp_sparkbot_board.cc`](../../esp_sparkbot_board.cc) |
| 开关 | `DEEP_DOG_MICROROS_ENABLE`（默认 **0**） |
| 契约 | a3 `docs/cloud_edge/REQUIREMENTS.md` 阶段 A · `docs/shared/TOPIC_CONTRACT.md` |

## 目标

ESP32-S3 deep-dog 作为 micro-ROS Client：

1. 关联实验室 WiFi（复用板级 WifiBoard / NVS 配网）
2. 经 UDPv4 连接 Agent（可配置 IP:端口，默认 8888）
3. 订阅 `/joint_group_effort_controller/joint_trajectory`（`trajectory_msgs/JointTrajectory`）
4. 以约 **50 Hz** 发布 `/joint_states`（`sensor_msgs/JointState`；阶段 A 允许 mock）
5. Agent 或 Client 重启后可重建 XRCE session

## 关节名（不可改）

```
L1_joint, L2_joint, L3_joint, L4_joint, L5_joint, L6_joint, L7_joint
```

## 技术选型

| 项 | 要求 |
|----|------|
| 组件 | [micro_ros_espidf_component](https://github.com/micro-ROS/micro_ros_espidf_component) **Humble**（Registry `>=22.0.0,<23.0.0`） |
| 传输 | UDPv4 over WiFi |
| Agent | Windows 主机局域网 IP（或同网段服务器）；**禁止**真机 `127.0.0.1`；一般不可用 WSL `172.28.x.x` |
| 配置 | Agent IP/端口：menuconfig；WiFi：配网页 / NVS（密钥不入库） |

## 边界（阶段 A 不做）

CAN 发送、200 Hz 插值、断连 disable 电机、gate、软限位、急停 GPIO（阶段 B / E2–E8、E10–E11）。

## 联调剖面建议

开启 `DEEP_DOG_MICROROS_ENABLE=1` 时建议关闭 `FACE_AI` / `VISION_HUB` / `HTTP_SERVER` 以省 internal SRAM；MQTT 可关可留。

## 验收

1. 服务器 `ros2 launch a3_cloud_edge micro_ros_agent.launch.py port:=8888` 运行中，Agent 日志可见 session
2. `ros2 topic hz /joint_states` ≈ 50 Hz（允许 mock position）
3. 服务器发布测试 `JointTrajectory` 后，固件日志/回调确认收到 `points`
4. 重启 Client 或 Agent 后可再次建立 session，无需改代码

联调步骤见 [`microros/README.md`](../../microros/README.md)（含「请回填 a3 QUICKSTART」文案）。

## 状态

| 项 | 状态 |
|----|------|
| 文档 | 本轮 |
| 固件 | 本轮（阶段 A） |
| 真机联调勾选 | 待实验室 Agent IP / WiFi |
