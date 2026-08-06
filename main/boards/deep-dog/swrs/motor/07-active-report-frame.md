# MOT-07 · 主动上报帧（通信类型 24）

| 项 | 内容 |
|----|------|
| ID | MOT-07 |
| 状态 | 本轮落地 |
| 依赖 | [MOT-01](./01-can-hw-profile.md) |
| 固件 | [`motor/protocol_motor`](../../motor/protocol_motor.cpp) · [`motor/deep_motor`](../../motor/deep_motor.cpp) · [`mqtt/modules/motor_mqtt`](../../mqtt/modules/motor_mqtt.cc) |

## 目标

使用 EL05 **通信类型 24（电机主动上报帧）** 开关电机 10ms 周期状态推送，替代业务层周期性 `sendRunModeForStatusQuery`（写 RUN_MODE 触发反馈的旧式做法）。旧 API **保留**供兼容；新路径推荐 `setActiveReportSwitch` / `DeepMotor::requestActiveReport`。

## CAN 协议（EL05）

### 开关帧（主机 → 电机）

| 字段 | 值 |
|------|-----|
| 29-bit ID bit28–24 | `0x18`（通信类型 24） |
| 29-bit ID bit23–8 | 主机 CAN_ID（`MOTOR_MASTER_ID`，默认 `0xFD`） |
| 29-bit ID bit7–0 | 目标电机 CAN_ID |
| dlc | 8 |
| data | `01 02 03 04 05 06 F_CMD 00` |
| F_CMD | `0x01` 开启主动上报；`0x00` 关闭（默认关闭） |

开启后电机约 **每 10ms** 主动推送状态帧。

### 上报帧（电机 → 主机）

| 字段 | 说明 |
|------|------|
| 29-bit ID bit28–24 | `0x18` |
| bit23–8 / bit7–0 / 故障位 / 模式位 | **与通信类型 2 反馈帧相同** |
| 8 字节 data | 角/速/矩/温，**与类型 2 相同**（16 位大端线性映射） |

固件在 `parseMotorData` 中对 cmd=2 与 cmd=0x18 共用数据区解析；`processCanFrame` 对两者同等更新 `has_feedback`、LED、扭矩观测等。

## 固件 API

| API | 说明 |
|-----|------|
| `MotorProtocol::setActiveReportSwitch(id, enable)` | 发送 T24 开关帧 |
| `MotorProtocol::sendRunModeForStatusQuery(id)` | **保留**；旧式 RUN_MODE 触发反馈 |
| `DeepMotor::requestActiveReport(id)` | ref++，0→1 时开启 |
| `DeepMotor::releaseActiveReport(id)` | ref--，1→0 时关闭 |

引用计数避免 `initializeMotor` 与 `motor/report_start` 互相 disable。

## MQTT

| Topic | 语义 |
|-------|------|
| `motor/report_start` | 对已注册电机 `requestActiveReport`（一次性，无 50ms 轮询） |
| `motor/report_stop` | 对已注册电机 `releaseActiveReport` |

`motor/status` 仍由 500ms 定时器从缓存发布；数据来源为 T24 上报帧。

## 验收

- [ ] `motor/report_start` 后 TX：`01 02 03 04 05 06 01 00`，cmd=0x18
- [ ] RX cmd=0x18，`motor/status` 角/速/矩持续更新
- [ ] `motor/report_stop` 后 TX：`... 00 00`
- [ ] `initializeMotor` 写零阶段通过 T24 反馈校验
- [ ] `sendRunModeForStatusQuery` 仍存在且未被删除
