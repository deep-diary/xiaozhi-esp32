# I09 · PC 桥 handle/cmd `output` 反馈（DS4 灯震 / Xbox 震）

| 项 | 内容 |
|----|------|
| 状态 | **契约 + 桥 DS4 HID + Xbox pygame rumble + 固件 PublishOutput** |
| 范围 | PC 桥执行 `handle/cmd` `action: output`：DS4 灯条 RGB + 双马达；Xbox 双马达（忽略 led） |
| 前置 | [I03](./I03-source-pc-mqtt-bridge.md) |
| 非目标 | 与板载 `led/cmd`（WS2812）混用；板载 BT `action: rumble`（见 [I02](./I02-source-bluepad32-xbox.md)） |

## 目标链路

```text
告警源 / 前端调试
  --PUB QoS1-->  …/handle/cmd  { action: output, … }
                    │
         ┌──────────┴──────────┐
         ▼                     ▼
   ESP32：忽略下行 output    PC 桥
   （可作发布方）              ├── DS4 hidapi → 灯 + 震
                              └── Xbox/pygame → rumble（led 忽略）
```

手柄插在电脑上时，**只有桥能写输出**。固件可 PUBLISH 同 Topic（告警）；下行 `output` 设备侧安静忽略（防自回环）。

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
| `led.r/g/b` | 0–255，可选；**仅 DS4**；Xbox 忽略 |
| `rumble.strong` / `weak` | [0,1] → 大/小马达（低频 / 高频）；省略为 0 |
| `duration_ms` | 可选；DS4 由桥定时清震；Xbox 传入 pygame `rumble(..., ms)` |
| 停震 | `rumble` 全 0 |

与板载 BT 的 `action: rumble`（I02，`weak`/`strong` 为 0–255）**并存**：日常剖面关板载 BT，前端 `source=wifi` 一律用 **`output`**。

## 桥行为

| 路径 | 启动 | output |
|------|------|--------|
| DS4 默认 HID | `python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan` | 灯 + 震 |
| Xbox / pygame | `--layout xbox` | 仅震 |
| 本地冒烟 | `--probe-output` / `--probe-xbox-rumble` | 不经 MQTT |

- 订 `handle/cmd`；仅处理 `action=output`
- **macOS**：Xbox 震动优先 **蓝牙**连 Mac；USB 有线常不震（Game Controller / SDL 限制）

## 固件

- `DeepDogHandleMqtt::PublishOutput(...)` → `Publish("handle/cmd", …)`
- 下行收到 `output`：安静忽略
- 联调钩：`action=disable` 末尾可发一次弱震 + 微红灯（桥在线时 DS4 可见）

## 验收

- [x] YAML 含 `action: output` 与 led/rumble 对象
- [x] 桥默认 HID + 订 cmd 写 DS4
- [x] 桥 pygame 路径 Xbox `rumble`（`--probe-xbox-rumble` / MQTT output）
- [x] 固件 PublishOutput；disable 联调钩
- [x] 前端需求 REQ-IOT-230：`source=wifi` 用 output；`rumble` 仅 bt
