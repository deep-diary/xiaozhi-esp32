# 10 · servo（裸舵机）

| 项 | 内容 |
|----|------|
| module_id | `servo` |
| capabilities | `servo` |
| 路由建议 | `/device/:deviceId/modules/servo` |
| 契约 | ready；驱动 + `servo_mqtt` + MCP |
| YAML | `servo/cmd`、`servo/status` |
| 说明 | 调试用 2 路裸舵机；产品云台走 [09-gimbal](./09-gimbal.md) |
| 驱动 | [servo/](../../../servo/)（MCPWM，移植 deep-diary，补 `duration_ms`） |

## 入口卡文案

- 标题：舵机  
- 说明：两路角度 / 类型 / 到位时间  
- 显示条件：`ext_pins.mode === "pwm"` **且** `capabilities.servo === true`

## 详情页目标

按 index 写角度、`duration_ms`、attach、type；列表回读 `angle` / `target` / `moving`。

## 引脚与剖面

| 项 | 值 |
|----|-----|
| 默认剖面 | `EXT_PIN=PWM` + `DEEP_DOG_SERVO_ENABLE=1`（见 [FEATURE_FLAGS.md](../../../FEATURE_FLAGS.md)） |
| index `0` | 自由引出脚 A = GPIO **38** |
| index `1` | 自由引出脚 B = GPIO **48** |
| 类型 | `90` / `180` / `270` / `360`（脉宽仍映射 500–2500µs） |

与 `EXT_PIN=CAN` 等互斥；云台产品层另开 `GIMBAL_ENABLE`。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `servo/status` | ↑ | 0 | true |
| `servo/cmd` | ↓ | 1 | false |

前缀：`deepdiary/deep-dog/{device_id}/`。连接成功订阅 cmd，并发一次 retain status；运动中节流约 200ms 补发，到位必发。

## 字段表

### `servo/cmd`（稀疏）

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `index` | int | 是 | `0` 或 `1` |
| `angle` | int | 否 | 目标角度，钳位到该路 `min`–`max` |
| `duration_ms` | int | 否 | `0` 或缺省 = **立即到位**；`>0` = 在该时间内 ease-out 插值 |
| `attach` | bool | 否 | `true` 绑定 PWM；`false` 释放 |
| `type` | enum | 否 | `90` \| `180` \| `270` \| `360`；可与 attach 同发 |
| `ts` | int | 否 | Unix 秒 |

### `servo/status`

| 字段 | 类型 | 说明 |
|------|------|------|
| `servos` | array[2] | 两路快照 |
| `servos[].index` | int | `0` / `1` |
| `servos[].angle` | int | 当前角度 |
| `servos[].target` | int | 目标角度（空闲时等于 `angle`） |
| `servos[].attached` | bool | 是否已绑定 PWM |
| `servos[].moving` | bool | 是否在插值运动中 |
| `servos[].type` | int | `90`/`180`/`270`/`360` |
| `servos[].min` / `max` | int | 角度范围 |
| `ts` | int | Unix 秒 |

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 样例 JSON

```json
{ "index": 0, "angle": 90, "duration_ms": 1000, "attach": true, "type": 180, "ts": 1710000000 }
```

```json
{
  "servos": [
    { "index": 0, "angle": 45, "target": 90, "attached": true, "moving": true, "type": 180, "min": 0, "max": 180 },
    { "index": 1, "angle": 90, "target": 90, "attached": true, "moving": false, "type": 180, "min": 0, "max": 180 }
  ],
  "ts": 1710000000
}
```

立即到位：`{ "index": 0, "angle": 0, "duration_ms": 0 }` 或省略 `duration_ms`。

## Steps（前端）

- **Step 1** 校验 `capabilities.servo`；建议同时看 `ext_pins.mode === "pwm"`。
- **Step 2** 订阅 `servo/status`。
- **Step 3** 列表 + 单路角度 / 类型 / `duration_ms` 滑条；发 `servo/cmd`。
- **Step 4** unmount 退订。

## 固件实现

| 项 | 路径 |
|----|------|
| MCPWM 驱动 | [`servo/Servo.c`](../../../servo/Servo.c) |
| 两路银行 | [`servo/servo_control.cc`](../../../servo/servo_control.cc) |
| MQTT | [`mqtt/modules/servo_mqtt.cc`](../../../mqtt/modules/servo_mqtt.cc) |
| MCP | [`servo/servo_mcp.cc`](../../../servo/servo_mcp.cc)：`self.servo.set_angle` / `set_type` / `attach` / `detach` / `get_status` |
| 板级 init | `esp_sparkbot_board.cc` → `DeepDogServoInit()` + `RegisterServoMcpTools` |

驱动选型：deep-diary **MCPWM**（非 otto LEDC）；时间插值参考 otto `MoveServos`，用 `esp_timer` 非阻塞。

## 验收

- [ ] 默认剖面 `ext_pins.mode=pwm` 且 `capabilities.servo=true`
- [ ] `servo/cmd` 设 `duration_ms=1000` 从 0→90，约 1s 内到位且 status `moving` 过渡
- [ ] `duration_ms=0` 立即到位
- [ ] `type` 切换后 `min`/`max` 与钳位正确
- [ ] MCP `self.servo.set_angle` 与 MQTT 一致
- [ ] 无 capability / 非 pwm 时前端隐藏入口卡
- [ ] 云台产品勿只引导本页（见 09-gimbal）
