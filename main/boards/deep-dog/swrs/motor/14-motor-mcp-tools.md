# MOT-14 · 电机 MCP 工具集收敛 + 粘性默认电机

| 项 | 内容 |
|----|------|
| ID | MOT-14 |
| 状态 | 本轮落地 |
| 依赖 | [MOT-03](./03-motor-mqtt.md) · [MOT-10](./10-motor-mcp-bridge.md) · [M02](../mqtt/M02-mcp-call-control-plane.md) |
| 固件 | `deep_motor_control` · `deep_motor` · `motor_mqtt` |

## 目标

1. **下线电机角度 LED 指示器**：删除 `motor/deep_motor_led_state.*`（临时方案，错放 motor 目录；无需求出处；板级以 `new DeepMotor(nullptr)` 构造，现网恒未生效）。电机语义不再耦合灯带，灯效统一归 `led/` 模块（`self.led_strip.*`）。
2. **工具命名统一**：电机 MCP 工具一律 `self.motor.*`；`self.can.*` 前缀不再注册电机语义工具。CAN 原始帧透传不受影响（仍走 MQTT `can/cmd`，见 [12-can](../mqtt/modules/12-can.md)）。
3. **工具归类去重**：删除与 4 个语义化模式工具 1:1 重复、且裸数字参数语音不可靠的 `set_mode`。
4. **粘性默认电机**：控制指令带 `motor_id` 时自动置为活跃电机；后续省略编号（`motor_id=0`）默认作用于活跃电机，支撑语音连续对话（"把 2 号电机转到 1rad" → "再转到 2rad"）。

## 1. 工具命名映射

| 旧名 | 新名 | 说明 |
|------|------|------|
| `self.can.send_motor_position` | `self.motor.set_position` | 位置模式（PARAM_LOC_REF，参数 ÷100 rad） |
| `self.can.send_motor_speed` | `self.motor.set_speed` | 速度模式目标速（PARAM_SPD_REF，参数 ÷10 rad/s） |
| `self.can.enable_motor` | `self.motor.enable` | CAN 使能（不切模式） |
| `self.can.reset_motor` | `self.motor.reset` | 停止/失能 |
| `self.can.control_motor` | `self.motor.control_mit` | MIT 运控帧（_x10 参数 ÷10） |

旧名不保留别名：`motor/tools` 为动态 catalog，前端调试页与 LLM 均实时获取新清单。

## 2. 工具分组（收敛后 ~25 个）

| 组 | 工具 |
|----|------|
| ① 总线/注册/发现 | `scan_bus`、`list`、`set_active`、`set_can_id`、`get_software_version` |
| ② 状态查询 | `get_status`、`print_all`、`start_status_task`、`stop_status_task` |
| ③ 初始化/使能 | `initialize`、`enable`、`reset` |
| ④ 模式切换 | `set_control_mode`、`set_position_mode`、`set_speed_mode`、`set_current_mode`、`set_zero_position` |
| ⑤ 运动控制 | `control_mit`、`set_position`、`set_speed` |
| ⑥ 示教录制 | `start_recording`、`stop_recording`、`play_recording`、`get_recording_status` |
| ⑦ 调试信号 | `start_sin_signal`、`stop_sin_signal` |

### 删除的工具

| 工具 | 理由 |
|------|------|
| `self.motor.set_mode` | 与 ④ 组 4 个模式工具功能 1:1 重复；`mode` 为裸整数 0-3，工具描述未给映射，语音"运控模式"无法可靠映射到数字；语义化工具名自描述、语音可靠 |
| `self.motor.enable_angle_indicator` | 角度 LED 指示器下线（见目标 1） |
| `self.motor.disable_angle_indicator` | 同上 |
| `self.motor.set_angle_range` | 同上 |
| `self.motor.get_angle_status` | 同上 |

### 保留但调整

- `set_active`：**保留**，语义升级为"设置默认（活跃）电机；后续省略 motor_id 的指令默认作用于该电机"；与控制工具一致，目标电机未注册时自动注册。
- `start_sin_signal` / `stop_sin_signal`：保留厂内正弦测试能力；删除其中的呼吸灯副作用调用与"呼吸灯"文案（灯带归 `led/` 模块）。

## 3. 粘性默认电机语义

### 状态

- `DeepMotor` 维护 `active_motor_id_`（-1 = 无）。
- **命令驱动**：仅显式命令改变活跃电机；CAN 反馈帧**不再**隐式覆盖（旧逻辑"最后上报者即活跃"在多机总线上抖动，删除）。
- 活跃电机写入点：
  - 带 `motor_id>0` 的控制/状态类 MCP 工具（注册成功后置活跃）；
  - `set_active` 显式设置（与控制工具一致，未注册时自动注册）；
  - 示教录制/播放开始（teaching 内部既有逻辑）；
  - `start_status_task`（既有逻辑）；
  - `clearAllMotors` 复位为 -1。

### MCP 工具参数约定

- 所有单电机工具的 `motor_id`：默认值 **0**，范围 **[0, 255]**。
- `motor_id=0`（或 LLM 省略）：解析为当前活跃电机；无活跃电机（≤0）时返回错误字符串："未指定电机编号且当前无活跃电机，请先指定电机编号"。
- `motor_id>0`：确保已注册（未注册则自动 `registerMotor`），注册成功后置为活跃电机，再执行。
- 工具描述统一补充："motor_id=0 或省略时作用于当前活跃电机（最近一次显式指定的电机）"。
- 无参工具（`scan_bus`、`list`、`stop_recording`、`stop_status_task`、`get_recording_status`）不涉及。

### MQTT 同步

- `motor/cmd`：`motor_id` 缺省或 0 时取活跃电机；无活跃电机则忽略并 warn。显式 `motor_id>0` 注册成功后置活跃。
- `motor/status`：根对象新增 `active_id`（int，-1 = 无活跃电机）。
- `motor/cmd.mcp_call` 白名单：仅 `self.motor.*`（移除 `self.can.*`）。

### 典型流程

```text
语音"把2号电机位置设为1rad" → self.motor.set_position{motor_id:2, position:100}
                              → register(2) → active=2 → 位置指令
语音"再转到2rad"（LLM 省略编号）→ self.motor.set_position{motor_id:0, position:200}
                              → motor_id=0 → 解析 active=2 → 位置指令
语音"切换到3号电机" → self.motor.set_active{motor_id:3} → active=3
```

## 4. 固件改动范围

| 文件 | 改动 |
|------|------|
| `motor/deep_motor_led_state.h/.cc` | **删除** |
| `motor/deep_motor.h/.cpp` | 删 LED/呼吸灯 11 个委托方法、`led_state_manager_` 成员、构造函数 `CircularStrip*` 参数、反馈帧 LED 推送段；删反馈帧隐式覆盖活跃电机 |
| `esp_sparkbot_board.cc` | `new DeepMotor(nullptr)` → `new DeepMotor()` |
| `motor/deep_motor_control.cc` | 删 5 工具、5 工具改名、motor_id 默认 0 + 活跃电机解析辅助、sin 去呼吸灯、分组注释、set_active 描述更新 |
| `mqtt/modules/motor_mqtt.cc` | 移除 `self.can.` catalog 上报与白名单；motor/cmd 支持 motor_id=0→活跃电机；status 加 active_id |

## 5. 边界 / 非目标

- 不动 CAN 原始透传链路（`can/cmd` topic、12-can、`protocol_motor.*`）。
- 不动 dog/leg/chassis 内部对 `DeepMotor` 的 C++ 调用（不走 MCP 工具名；整机运控不受粘性默认电机影响）。
- 灯带效果统一由 `led/` 模块承担；本次不为电机重建任何灯效。
- `motor_target_angles_` / `getMotorTargetAngle` 保留（`motor/status.target_rad` 在用）。

## 6. 验收

- [ ] 编译通过；无 `DeepMotorLedState` / `led_state_manager_` / `self.can.` 残留引用（swrs 文档除外）
- [ ] `motor/tools` catalog 中无 `self.can.*`、无角度 LED 工具、无 `set_mode`；5 个工具为新名
- [ ] `motor/cmd.mcp_call` 调旧名 `self.can.*` 返回 not allowed；调 `self.motor.*` 正常
- [ ] 显式 `motor_id=2` 控制后 `motor/status.active_id=2`；随后 `motor_id=0` 指令作用于 2 号
- [ ] 无活跃电机时 `motor_id=0` 返回"请先指定电机编号"
- [ ] sin 信号工具执行后无任何灯带/呼吸灯调用
