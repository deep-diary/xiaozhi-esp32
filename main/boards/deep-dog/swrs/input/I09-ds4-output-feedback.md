# I09 · DS4 手柄 output 反馈（灯条 / 震动）

| 项 | 内容 |
|----|------|
| 状态 | **契约 + 桥默认 HID + 固件 PublishOutput** |
| 范围 | PC 桥写 DS4 HID Output `0x05`（灯条 RGB + 双马达）；告警/场景经 MQTT 触发 |
| 前置 | [I03](./I03-source-pc-mqtt-bridge.md)（默认 HID 全量读） |
| 非目标 | 与板载 `led/cmd`（WS2812）混用；Xbox；触控手势 I08 |

## 目标链路

```text
告警源（固件 App / 云 / 前端调试）
  --PUB QoS1-->  …/handle/cmd  { action: output, … }
                    │
         ┌──────────┴──────────┐
         ▼                     ▼
   ESP32：忽略下行 output    PC 桥（默认 HID）
   （可作发布方）              └── write 0x05 → DS4
```

DS4 插在电脑上，**只有桥能写 HID**。固件职责是 **发 `output`**（或云端直接发同 Topic）。

## 契约 · `handle/cmd` `action: output`

```json
{
  "action": "output",
  "led": { "r": 0, "g": 80, "b": 255 },
  "rumble": { "strong": 0.6, "weak": 0.2 },
  "duration_ms": 400,
  "ts": 1710000000
}
```

| 字段 | 说明 |
|------|------|
| `led.r/g/b` | 0–255，稀疏可选；省略则桥保持上次灯色（首次默认微蓝） |
| `rumble.strong` / `weak` | [0,1] → 大/小马达；省略为 0 |
| `duration_ms` | 可选；到期桥自动清震（灯保留） |
| 停震 | `rumble` 全 0 |

与板载 BT 的 `action: rumble`（I02，`weak`/`strong` 为 0–255 整型）**并存**：`output` 给 **PC 桥 DS4**；`rumble` 给 **板载 Xbox**（planned）。

## 桥行为

- **默认** HID 全量读（原 `--touchpad-xy`）；`--no-touchpad-xy` 回退 pygame（无灯震）
- 订 `handle/cmd`；仅处理 `action=output`
- 启动：`python3 scripts/deep_dog_handle_bridge.py --via lan`

## 固件

- `DeepDogHandleMqtt::PublishOutput(...)` → `Publish("handle/cmd", …)`
- 下行收到 `output`：**安静忽略**（防自回环）
- 联调钩：`action=disable` 末尾发一次弱震 ~200ms + 微红灯

真实告警（倾倒/堵转等）在对应 App 调同一 API。

## 验收

- [x] YAML 含 `action: output` 与 led/rumble 对象
- [x] 桥默认 HID + 订 cmd 写 0x05
- [x] 固件 PublishOutput；disable 联调钩
- [x] 前端需求 REQ-IOT-230 调试按钮
