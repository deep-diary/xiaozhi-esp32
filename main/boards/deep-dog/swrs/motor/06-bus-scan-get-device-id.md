# MOT-06 · 总线扫描（通信类型 0 · 获取设备 ID）

| 项 | 内容 |
|----|------|
| ID | MOT-06 |
| 状态 | 本轮落地 |
| 依赖 | [MOT-01](./01-can-hw-profile.md) |
| 固件 | [`motor/protocol_motor`](../../motor/protocol_motor.cpp) · [`motor/deep_motor`](../../motor/deep_motor.cpp) · [`mqtt/modules/motor_mqtt`](../../mqtt/modules/motor_mqtt.cc) |

## 目标

通过 EL05 **通信类型 0（获取设备 ID）** 异步发现 CAN 总线上的电机，消除协议层与 `CanRxTask` 双读 RX 队列的竞争；探测帧只发不等，应答由统一收帧路径解析并注册电机槽位。

## CAN 协议（EL05）

### 请求帧（主机 → 电机）

| 字段 | 值 |
|------|-----|
| 29-bit ID bit28–24 | `0`（通信类型 0） |
| 29-bit ID bit23–8 | 主机 CAN_ID（`MOTOR_MASTER_ID`，默认 `0xFD`） |
| 29-bit ID bit7–0 | 目标电机 CAN_ID（1～127） |
| 8 字节 data | **全 0** |
| dlc | 8 |

### 应答帧（电机 → 主机）

| 字段 | 值 |
|------|-----|
| 29-bit ID bit28–24 | `0` |
| 29-bit ID bit23–8 | 电机 CAN_ID |
| 29-bit ID bit7–0 | 固定 `0xFE` |
| 8 字节 data | 64 位 MCU 唯一标识符（大端序组装为 `uint64_t`） |

> 应答 ID 布局与反馈帧（cmd=2）不同，解析时须单独分支，不可复用 error/mode 位域宏。

## 固件架构

```text
sendBusScanProbes()          CanRxTask::readFrame
        │                              │
        ▼                              ▼
MotorProtocol::sendGetDeviceIdProbes   DeepMotor::processCanFrame
        │                              │
        └── CAN TX（只发） ──────────────┴── parseMotorData(cmd=0)
                                              registerMotorId + mcu_uid
```

- **发送**：`MotorProtocol::sendGetDeviceIdProbe(s)` / `DeepMotor::sendBusScanProbes`
- **接收**：仅 `CanRxTask` 读帧 → `DeepMotor::processCanFrame` → `MotorProtocol::parseMotorData`
- **状态**：`motor_status_t.has_device_id`、`motor_status_t.mcu_uid`

## MQTT

| Topic | 方向 | 语义 |
|-------|------|------|
| `motor/scan` | ↓ | 触发异步扫描（payload 可为空 `{}`） |
| `motor/scan_result` | ↑ | 扫描启动：`{ "started": true, "range": [1,127], "ts": ... }`；发现事件：`{ "event": "discovered", "id": N, "mcu_uid": "...", "ts": ... }` |
| `motor/status` | ↑ retain | 发现新电机后自动更新；`motors[].mcu_uid` 在 `has_device_id` 时输出 |

## 验收

- [ ] `motor/scan` 立即返回，无 6s+ 阻塞
- [ ] 探测 TX：dlc=8，data 全 0
- [ ] RX 应答 id 低字节 `0xFE`，仅 `CanRxTask` 读帧
- [ ] 应答到达后 `motor/status` retain 含新 `id` 与 `mcu_uid`
- [ ] 无双读竞争（协议层不再 `readFrame` 等待）
