# MOT-03 · 电机控制 MQTT

| 项 | 内容 |
|----|------|
| ID | MOT-03 |
| 状态 | 本轮新建契约并落地固件 |
| 权威模块页 | [mqtt/modules/14-motor.md](../mqtt/modules/14-motor.md) |
| YAML | `motor/cmd`、`motor/status` |
| 驱动 | [`motor/`](../../motor/) · `DeepMotor` / `MotorProtocol` |

## 目标

单电机调试页：经 MQTT 使能、位置/限速/电流（及可选 MIT）点动，并回读反馈。  
与 CAN 透传互补：本模块走**协议语义**；`can/*` 走**原始帧**。

## Topic 摘要

| Topic | 方向 | 用途 |
|-------|------|------|
| `motor/status` | ↑ retain | 已注册电机 **完整** `motor_status_t` 快照（见 14-motor 字段表） |
| `motor/cmd` | ↓ 稀疏 | enable / reset / position / speed_limit / iq_ref / MIT / set_zero / teaching |
| `motor/scan` | ↓ | 异步总线 ID 扫描（见 MOT-06） |
| `motor/scan_result` | ↑ | 扫描启动 / 发现事件 |
| `motor/report_start` / `motor/report_stop` | ↓ | 主动上报 T24 开关（见 MOT-07） |

位置量程：`P_MIN`～`P_MAX` = **±12.57 rad**（协议常量）。

字段以 [14-motor](../mqtt/modules/14-motor.md) 与 YAML 为准。

## 验收

- [ ] `capabilities.motor` 入口卡可见
- [ ] `motor/cmd` 使能指定 `motor_id` 后 `motor/status` 有反馈
- [ ] 位置点动与 MCP `self.motor.*` 不互相破坏（并存）
