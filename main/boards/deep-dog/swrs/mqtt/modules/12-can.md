# 12 · can（CAN 透传）

| 项 | 内容 |
|----|------|
| module_id | `can` |
| capabilities | `can` |
| 路由建议 | `/device/:deviceId/modules/can` |
| 契约 | ready；固件本轮落地（MOT-02） |
| 需求 | [swrs/motor/02](../../motor/02-can-mqtt.md) |
| YAML | `can/cmd`、`can/status`、`can/frames`、`can/tx` |
| 硬件 | GPIO38/48 TWAI（sparkbot 原 UART 脚） |
| 网页对齐 | deep-trace `80-can-web-tunnel` |

## 入口卡文案

- 标题：CAN  
- 说明：总线帧透传监视  

## 详情页目标

开关 tunnel、看 `can/status`、滚动显示 `can/frames`；**默认禁止**网页注入（`allow_tx`）。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `can/status` | ↑ | 0 | true |
| `can/frames` | ↑ | 0 | false |
| `can/cmd` | ↓ | 1 | false |
| `can/tx` | ↓ | 1 | false | 仅 `allow_tx=true` |

## 样例 JSON

**cmd（开透传 + 节流）**

```json
{
  "tunnel": true,
  "mirror_tx": true,
  "allow_tx": false,
  "max_hz": 50,
  "batch_max": 32,
  "ext_only": true,
  "ts": 1710000000
}
```

**frames**

```json
{
  "frames": [
    {
      "dir": "rx",
      "id": 305419896,
      "id_hex": "0x12345678",
      "ext": true,
      "rtr": false,
      "dlc": 8,
      "data_hex": "0102030405060708",
      "ts_ms": 123456
    }
  ],
  "dropped": 0,
  "ts": 1710000000
}
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。禁止默认全量无过滤透传。

## Steps（前端）

- **Step 1** 校验 `capabilities.can`。
- **Step 2** 订阅 `can/status`；用户打开监视后再订 `can/frames`（避免无谓刷屏）。
- **Step 3** UI：tunnel 开关、帧表、dropped；`allow_tx` 默认关且强警示。
- **Step 4** 发 `can/cmd`；仅明确确认后才用 `can/tx`。
- **Step 5** unmount 退订（尤其 `can/frames`）。

## 固件实现

- `mqtt/modules/can_mqtt.*`；挂钩 `ESP32Can` 嗅探/打包；见 [can/README](../../../can/README.md)。

## 验收

- [ ] 详情页可开 tunnel 看帧（桩阶段可 mock）
- [ ] 文档含 allow_tx 危险提示
- [ ] 无 capability 隐藏入口卡
