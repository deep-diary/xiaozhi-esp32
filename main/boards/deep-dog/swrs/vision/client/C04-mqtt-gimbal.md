# V-C04 · MQTT 云台控制（更后）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C04** |
| 优先级 | 低（不插队 C02～C03） |
| 依赖 | [C03](./C03-mqtt-stream-control.md) MQTT 客户端、[M01](../../mqtt/M01-board-mqtt-protocol.md) |
| 协议真源 | [`../../mqtt/protocol/deep-dog-mqtt.yml`](../../mqtt/protocol/deep-dog-mqtt.yml) `gimbal/*`、`servo/*` |
| 参考实现 | [`deep-diary/gimbal`](../../../../deep-diary/gimbal/)、[`deep-diary/servo`](../../../../deep-diary/servo/) |
| 状态 | 协议已定；引脚与供电需 deep-dog 实机确认后再实现 |

## 目标

MQTT 下发 pan/tilt（绝对/相对），两路舵机 PWM，软件限位；上报当前角度。产品路径用 `gimbal/*`；裸舵机调试用 `servo/*`（见 M01）。

## 协议

前缀：`deepdiary/deep-dog/<device_id>/`。完整字段见 YAML。

### `gimbal/cmd`（↓ QoS=1）

```json
{ "mode": "absolute" | "relative", "pan": 135, "tilt": 90, "speed": 0, "ts": 1710000000 }
```

| 字段 | 说明 |
|------|------|
| `mode` | `absolute`：绝对角；`relative`：`pan`/`tilt` 为增量（度） |
| `pan` / `tilt` | 与 diary `Gimbal_setAngles` / `move*` 对齐；默认限位 pan `[0,270]`、tilt `[0,180]` |
| `speed` | 可选；0 表示尽快到位，非 0 可映射步进延时（实现时再定） |

**不**采用 deep-diary Thumbler 的 `tar_pitch` / `tar_roll` 命名。

### `gimbal/status`（↑ QoS=0，retain）

```json
{
  "pan": 135,
  "tilt": 90,
  "ready": true,
  "lim_pan": [0, 270],
  "lim_tilt": [0, 180],
  "ts": 1710000000
}
```

对齐 `Gimbal_getAngles`。

### 与 `servo` 模块

云台依赖舵机驱动；Web 控云台应走 `gimbal/*`。多路裸舵机调试见 M01 `servo/cmd|status`。

## 说明

- 历史 diary GPIO（如 19/20 或 38/48）与 deep-dog CAN/其它外设可能冲突，**实现前单独确认空闲脚与供电**。
- 可与人脸跟脸（`track/cmd` + `face/status`）联动，非本切片范围。
- `capabilities.gimbal=false` 时不订阅/不发布本域 Topic。

## 验收

- [ ] MQTT 绝对角可到位，status 回读一致
- [ ] relative 移动受软件限位
- [ ] 未 ready / 非法 JSON 不崩溃
- [ ] 引脚文档已写入板级 `config.h`（实现时）
