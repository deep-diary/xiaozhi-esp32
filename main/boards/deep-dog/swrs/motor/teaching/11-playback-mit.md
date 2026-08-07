# MOT-11 · 示教 MIT 轨迹播放

| 项 | 内容 |
|----|------|
| ID | MOT-11 |
| 状态 | ready |
| 固件 | [`deep_motor_teaching.cpp`](../../../motor/deep_motor_teaching.cpp) `playTask` |
| 上级 | [teaching/README.md](./README.md) |

## 三阶段

| 阶段 | 动作 |
|------|------|
| 准备 | `setMotorControlMode` + `enableMotor` |
| Phase A | 当前角 → 首点，`blend_ms`（默认 3000ms） |
| Phase B | 轨迹 MIT：v2 按 `t_ms` 时间轴；legacy 按 `duration_ms` 均匀拉时 |

## 参数

见 [12-record-v2.md](./12-record-v2.md) `TeachingPlayConfig`。

MCP `self.motor.play_recording` · MQTT `teaching: play` + `play_*` / `play_time_scale`。

## 编译

`DEEP_DOG_USE_MIT_WALK=0` 时 `executeTeaching*` 返回 false。
