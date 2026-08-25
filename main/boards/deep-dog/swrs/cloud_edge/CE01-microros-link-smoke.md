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

## 实现约束

- 建链前探测 Agent **必须**用 `rmw_uros_ping_agent_options`，并把 menuconfig 的 Agent IP/端口写入 `rmw_init_options`。裸 `rmw_uros_ping_agent()` 会打到 libmicroros 编译期默认 **`127.0.0.1:8888`**，真机永远连不上实验室 Agent。
- 勿调用 `uros_network_interface_initialize()`（会另起一套 STA，与板级 WifiBoard / NVS 配网冲突）。STA 已获 IP 后再 `DeepDogMicrorosStart()`。
- Session 已建立后**禁止**再调无 options 的 `rmw_uros_ping_agent()`：它仍走 libmicroros 默认 `127.0.0.1`，会在 Agent 实际在线时误拆 session。保活改为连续 `rcl_publish` 失败再重连；Agent 重启后由外层 `ping_agent_options` 重建。
- `/joint_states` @ 50 Hz 与轨迹订阅均用 **BEST_EFFORT**。JointState 默认 RELIABLE 会打满 XRCE 输出历史；轨迹若 RELIABLE，实验室 `ros2 topic pub` 能在 DDS 图上匹配到 `deep_dog_microros`，但 XRCE inbound 常无回调（与 `motor_protocol_node` 的 BE 订阅不一致）。
- 轨迹 `micro_ros_utilities` 容量：string ≥64、sequence ≥16，避免 7 关节名 + 嵌套 `positions` 反序列化失败而静默丢包。
- **产品侧为 a3 机械臂**（`a3_arm_ws`，7 关节 `L1_joint`…`L7_joint`）。阶段 A 入站 **drain 全量、业务处理限 50 Hz**（`DEEP_DOG_MICROROS_TRAJ_HANDLE_PERIOD_MS`）；7 关节测试帧不限流。
- 冒烟前停掉会抢同话题的杂节点（如 `microros_mock_client`）；勿再按四足/`trotbot`/`CHAMP` 联调假设验收。
- 关 `VISION_HUB`/`FACE_AI` 时跳过 `InitializeCamera()`，**不得**把板级 MQTT 构造绑在该函数里（否则 `board_mqtt_` 为空，broker 无客户端）。
- 固件回调必须节流日志；验收看到 `traj n=` 即通过。用 7 关节测试帧时看 `joint_names=7` / `pos0=0.5`。

## 边界（阶段 A 不做）

CAN 发送、200 Hz 插值、断连 disable 电机、gate、软限位、急停 GPIO（阶段 B / E2–E8、E10–E11）。

## 联调剖面建议

开启 `DEEP_DOG_MICROROS_ENABLE=1` 时用 **单电机剖面** 并关掉视觉，给 XRCE 腾 internal SRAM：

| 宏 | 值 |
|----|-----|
| `EXT_PIN_MODE` | `CAN`（GPIO38 TX / 48 RX） |
| `CAN_ENABLE` / `MOTOR_ENABLE` | 1 |
| `DOG` / `GIMBAL` | 0 |
| `FACE_AI` / `VISION_HUB` / `TRACK_MQTT` / `HTTP_SERVER` | 0 |
| MQTT | 可留（电机联调需要） |

CE01 阶段 A **仍不**把轨迹转发到 CAN（见边界）。本剖面只是 MOT-01 硬件 + 关视觉，与 CE01 冒烟并存。

视觉关时跳过 `InitializeCamera()`（无摄像头的板子还会少两次 SCCB 失败重试）。

## 预编译 libmicroros（Windows / 跳过编库）

| 项 | 说明 |
|----|------|
| 路径 | [`tools/microros_prebuilt/`](../../../../tools/microros_prebuilt/)（git 跟踪，~26MB） |
| 内容 | `libmicroros.a` + `include/` + `include_override/` + `MANIFEST.json` |
| 安装 | `python scripts/deep_dog/install_microros_prebuilt.py`（在 `deep_dog_fetch_microros.sh` 之后） |
| Win 补丁 | `python scripts/deep_dog/patch_microros_windows_cmake.py` |
| 验收 | 日志 `[deep-dog] using existing libmicroros.a`，不跑 WSL/`libmicroros.mk` 长编 |

`MANIFEST.json` 校验 `app-colcon.meta` sha256；改 meta 或升级组件后须在 Mac/Linux **重编并更新** prebuilt。

**编库环境**：macOS/Linux 可原生 `make -f libmicroros.mk`；Windows 原生易失败，缺 prebuilt 时补丁改走 WSL（Linux 环境）编库——与整机固件在 Win+IDF 上编不矛盾。

## 验收

1. 服务器 `ros2 launch a3_cloud_edge micro_ros_agent.launch.py port:=8888` 运行中，Agent 日志可见 session
2. 图上可见节点 `deep_dog_microros` 发布 `/joint_states`（允许 mock position）。独占该话题时应 ≈ 50 Hz；有其它节点同发时以 `ros2 topic info -v` 为准，勿用混合 `topic hz`
3. 服务器发布 **7 关节**测试 `JointTrajectory` 后，固件日志/回调确认收到 `points`（`joint_names=7`）
4. 重启 Client 或 Agent 后可再次建立 session，无需改代码

联调步骤见 [`microros/README.md`](../../microros/README.md)（含「请回填 a3 QUICKSTART」文案）。

## 状态

| 项 | 状态 |
|----|------|
| 文档 | 本轮 |
| 固件 | 本轮（阶段 A） |
| 真机联调勾选 | STA `192.168.3.111` · Agent session 稳定 · a3 臂 7 关节契约 · 轨迹业务处理限 50 Hz · 关视觉后 MQTT 须独立于 Camera 构造 |
