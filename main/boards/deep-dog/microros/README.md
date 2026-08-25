# deep-dog micro-ROS Client（CloudEdge CE01）

阶段 A：WiFi STA → Agent UDP → 订 `JointTrajectory`、50 Hz 发 `JointState`（可 mock）。  
需求：[swrs/cloud_edge/CE01](../swrs/cloud_edge/CE01-microros-link-smoke.md)。契约：a3_arm_ws `TOPIC_CONTRACT.md`（勿擅自改话题/关节名）。

## 开关

| 项 | 值 |
|----|-----|
| CMake | `-DDEEP_DOG_MICROROS=ON`（同时定义 `DEEP_DOG_MICROROS_ENABLE=1` 并链接组件） |
| 默认 | OFF（日常 MQTT/视觉构建不编 `libmicroros`） |
| 组件 | 不进默认 `idf_component.yml`；联调前执行 `scripts/deep_dog/deep_dog_fetch_microros.sh`（Humble `>=22.0.0,<23.0.0`） |

联调剖面建议：关 `FACE_AI` / `VISION_HUB` / `HTTP_SERVER` 省 SRAM。

## 编译环境

```bash
# 加载 ESP-IDF（勿 source ROS 2 setup）
get_idf55   # 或 . $IDF_PATH/export.sh

# IDF 所用 Python venv 内：
pip install catkin_pkg colcon-common-extensions lark

# 仅 CloudEdge 联调需要：拉取 Humble 组件（一次）
./scripts/deep_dog/deep_dog_fetch_microros.sh
# Windows 另执行：
#   python scripts/deep_dog/patch_microros_windows_cmake.py

idf.py set-target esp32s3
idf.py -DDEEP_DOG_MICROROS=ON build
```

### Windows 注意（本机为 Windows + WSL）

`libmicroros.mk` 依赖 Unix `make` + LF 行尾；在 Windows NTFS/`/mnt/d` 上直接编常因 CRLF / path 失败。

推荐任选其一：

1. **Linux / 原生 WSL 文件系统**内打开本仓库（或把工程拷到 `~/xiaozhi-esp32`）再 `idf.py -DDEEP_DOG_MICROROS=ON build`
2. Windows 侧先拉组件，再补丁 CMake 并预编库：
   ```powershell
   idf.py -DDEEP_DOG_MICROROS=ON reconfigure   # 拉取 managed_components
   python scripts/deep_dog/patch_microros_windows_cmake.py
   wsl bash scripts/deep_dog/wsl_build_libmicroros.sh   # 须在 LF/原生盘成功
   idf.py -DDEEP_DOG_MICROROS=ON build
   ```

组件更新后需重新运行 `patch_microros_windows_cmake.py`。

首次编 `libmicroros.a` 需联网、耗时长。改工程根 [`app-colcon.meta`](../../../../app-colcon.meta) 后执行：

```bash
idf.py clean-microros   # 若无此目标：删 managed_components/.../libmicroros.a 与 micro_ros_src 再编
idf.py -DDEEP_DOG_MICROROS=ON build
```

## menuconfig（Agent）

```bash
idf.py -DDEEP_DOG_MICROROS=ON menuconfig
```

路径：**micro-ROS Settings**

| 项 | 说明 |
|----|------|
| middleware | Micro XRCE-DDS |
| Agent IP | **Windows 主机局域网 IP**（与板同 WiFi）；禁止 `127.0.0.1`；勿用 WSL `172.28.x.x` |
| Agent Port | 默认 `8888` |

组件里的 **WiFi SSID/Password** 对本板**无效**：WiFi 走板级配网页 / NVS（`WifiBoard`），密钥不入库。勿调用 `uros_network_interface_initialize`。

## 烧录与监控

```bash
idf.py -DDEEP_DOG_MICROROS=ON -p COMx flash monitor
```

期望日志：`DogMicroros: Agent <ip>:8888` → `session ready` → 收到轨迹时 `traj points=...`。

## 话题（契约）

| 方向 | 话题 | 类型 |
|------|------|------|
| 订 | `/joint_group_effort_controller/joint_trajectory` | `trajectory_msgs/JointTrajectory` |
| 发 | `/joint_states` | `sensor_msgs/JointState` @ 50 Hz |

关节名：`L1_joint` … `L7_joint`。

---

## 请回填 a3_arm_ws `docs/cloud_edge/QUICKSTART.md`

> 以下段落供人工粘贴进契约仓 QUICKSTART「外置 ESP32 固件对接」；**固件 Agent 不直接改 a3_arm_ws**。

### 真机联调（xiaozhi-esp32 / deep-dog）

1. **WSL**：`ros2 launch a3_cloud_edge micro_ros_agent.launch.py port:=8888`（监听 `0.0.0.0:8888`）。
2. 若板在实验室 WiFi：在 Windows 做 `netsh interface portproxy`（或镜像网络），把主机 `:8888` 转到 WSL；固件填 **Windows LAN IP:8888**（见 `docs/dev/WSL2_SETUP.md`）。
3. **停止** `microros_mock_client`（避免与真机抢同一话题）。
4. 固件仓：
   - 配网页写入实验室 WiFi（SSID/密码不进 git）
   - `idf.py -DDEEP_DOG_MICROROS=ON menuconfig` 设 Agent IP/端口
   - `idf.py -DDEEP_DOG_MICROROS=ON build flash monitor`
5. 验收：
   - Agent 日志可见 session
   - `ros2 topic hz /joint_states` ≈ 50 Hz
   - 发测试轨迹后固件日志出现 `traj points=...`：

```bash
ros2 topic pub --once /joint_group_effort_controller/joint_trajectory \
  trajectory_msgs/msg/JointTrajectory \
  "{joint_names: [L1_joint,L2_joint,L3_joint,L4_joint,L5_joint,L6_joint,L7_joint], \
    points: [{positions: [0,0.5,-0.5,0,0,0,0], time_from_start: {sec: 2}}]}"
```

6. 重启板或 Agent 后应能自动重连，无需改代码。

固件实现路径：`main/boards/deep-dog/microros/`；需求切片：`swrs/cloud_edge/CE01-microros-link-smoke.md`。
