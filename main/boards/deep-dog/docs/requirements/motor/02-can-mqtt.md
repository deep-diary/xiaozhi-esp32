# MOT-02 · CAN MQTT 透传

| 项 | 内容 |
|----|------|
| ID | MOT-02 |
| 状态 | 契约 ready（见 12-can）；固件本轮落地 |
| 权威模块页 | [mqtt/modules/12-can.md](../mqtt/modules/12-can.md) |
| YAML | `cmd/can`、`status/can`、`status/can/frames`、`cmd/can/tx` |
| 前端 | deep-trace `modules/can/80-can-web-tunnel` 等 |

## 目标

网页 CAN 详情页：开 tunnel 监视帧；确认后可 `allow_tx` 经 MQTT 注入原始帧。

**联调剖面（当前默认）**：固件 `tunnel=true`、`allow_tx=true`、`ext_only=false`（标准帧与扩展帧都上报），
网页零配置即可看帧 / 注帧；并默认每 **2 s** 注入一条**心跳帧**（`id_hex=0x1F000000`，模拟 RX，**不写总线**）
用于自检透传链路。**量产 / 正式版本必须**改回安全默认：`allow_tx=false`、`ext_only=true`、
`DEEP_DOG_CAN_HEARTBEAT_MS=0`。

## Topic 摘要

| Topic | 方向 | 用途 |
|-------|------|------|
| `status/can` | ↑ retain | tunnel / allow_tx / 引脚 / 错误计数 |
| `status/can/frames` | ↑ | 节流批量帧（开 tunnel 后） |
| `cmd/can` | ↓ | 开关 tunnel、限速、`allow_tx` |
| `cmd/can/tx` | ↓ | 仅 `allow_tx=true` 时注入 |

字段与样例以 [12-can](../mqtt/modules/12-can.md) 与 [YAML](../mqtt/protocol/deep-dog-mqtt.yml) 为准。

## 固件

- 模块：`mqtt/modules/can_mqtt.*`
- 挂钩：`ESP32Can` 嗅探 / `writeFrame`
- 心跳：`DeepDogCanMqtt::EmitHeartbeat()`，周期宏 `DEEP_DOG_CAN_HEARTBEAT_MS`（`can/can_config.h`）
- 门控：`#if DEEP_DOG_CAN_ENABLE`

## 验收

- [ ] `capabilities.can` 时入口卡可见
- [ ] `cmd/can` `tunnel=true` 后可见 `status/can/frames`（电机反馈扩展帧）
- [ ] 空总线（无真实报文）时仍可见心跳帧 `0x1F000000`
- [ ] 未确认 `allow_tx` 时 `cmd/can/tx` 被拒
- [ ] unmount 退订 `status/can/frames`（前端）
