# MOT-09 · 异步电机初始化

| 项 | 内容 |
|----|------|
| ID | MOT-09 |
| 状态 | 本轮落地 |
| 依赖 | [MOT-03](./03-motor-mqtt.md) · [MOT-07](./07-active-report-frame.md) |
| 固件 | [`motor/deep_motor`](../../motor/deep_motor.cpp) · `motor_config.h` |

## 背景

旧版电机固件可能**无 T24 主动上报**，同步 `initializeMotor` 在 5×80ms 内等不到反馈即失败。异步路径只发指令、在 RX 解析环中判定零位。

## 行为

| 阶段 | `init_state` | 说明 |
|------|--------------|------|
| 未初始化 | `none` | 默认 |
| 已发指令、等反馈 | `initializing` | reset → set_zero → mode → enable（不阻塞等零位） |
| 收到反馈且 \|pos\|≤tol | `ready` | 启用扭矩观测；允许位置/电流下发 |
| 超时（默认 30s） | `failed` | reset 停扭 |

配置（`motor_config.h`）：

| 宏 | 默认 | 说明 |
|----|------|------|
| `DEEP_DOG_MOTOR_INIT_ASYNC` | `1` | `0` 恢复同步阻塞校验 |
| `DEEP_DOG_MOTOR_INIT_ZERO_TOL_RAD` | `0.15` | 零位容差 rad |
| `DEEP_DOG_MOTOR_INIT_TIMEOUT_MS` | `30000` | 异步超时 |

旧固件兼容：异步启动后 `requestActiveReport` + 偶发 `sendRunModeForStatusQuery` 拉反馈。

## MQTT

`motor/status.motors[].init_state`：`none` | `initializing` | `ready` | `failed`

`motor/cmd.enable=true` 在异步模式下立即返回（状态变为 `initializing`），前端应订 status 等待 `ready`。

## 验收

- [ ] 无主动上报的旧固件：enable 后 30s 内 status 变 `ready`（或明确 `failed`）
- [ ] `ready` 前发 `position_rad` 有 WARN 且不影响后续 init 完成
- [ ] `DEEP_DOG_MOTOR_INIT_ASYNC=0` 行为与旧同步路径一致
