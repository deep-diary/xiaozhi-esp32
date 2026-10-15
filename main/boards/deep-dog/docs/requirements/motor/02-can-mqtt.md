# MOT-02 · CAN MQTT 透传

| 项 | 内容 |
|----|------|
| ID | MOT-02 |
| 状态 | 契约 ready（见 12-can）；固件本轮落地 |
| 权威模块页 | [mqtt/modules/12-can.md](../mqtt/modules/12-can.md) |
| YAML | `can/cmd`、`can/status`、`can/frames`、`can/tx` |
| 前端 | deep-trace `modules/can/80-can-web-tunnel` 等 |

## 目标

网页 CAN 详情页：开 tunnel 监视帧；确认后可 `allow_tx` 经 MQTT 注入原始帧。  
**默认 `allow_tx=false`**，禁止默认网页写总线。

## Topic 摘要

| Topic | 方向 | 用途 |
|-------|------|------|
| `can/status` | ↑ retain | tunnel / allow_tx / 引脚 / 错误计数 |
| `can/frames` | ↑ | 节流批量帧（开 tunnel 后） |
| `can/cmd` | ↓ | 开关 tunnel、限速、`allow_tx` |
| `can/tx` | ↓ | 仅 `allow_tx=true` 时注入 |

字段与样例以 [12-can](../mqtt/modules/12-can.md) 与 [YAML](../mqtt/protocol/deep-dog-mqtt.yml) 为准。

## 固件

- 模块：`mqtt/modules/can_mqtt.*`
- 挂钩：`ESP32Can` 嗅探 / `writeFrame`
- 门控：`#if DEEP_DOG_CAN_ENABLE`

## 验收

- [ ] `capabilities.can` 时入口卡可见
- [ ] `can/cmd` `tunnel=true` 后可见 `can/frames`（电机反馈扩展帧）
- [ ] 未确认 `allow_tx` 时 `can/tx` 被拒
- [ ] unmount 退订 `can/frames`（前端）
