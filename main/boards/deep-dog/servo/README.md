# deep-dog 舵机（servo）

两路标准舵机，自由引出脚 **PWM** 模式：GPIO38 / GPIO48。

## 驱动选型

| 方案 | 外设 | 说明 |
|------|------|------|
| **本板采用** | ESP32 **MCPWM** 50Hz | 移植自 [deep-diary/servo](../../deep-diary/servo/)，compare 值直接对应脉宽 µs |
| otto-robot | LEDC | 双足 6 路 + 运动库；本板只借鉴「按时插值」思路，不整包复用 |

脉宽：`500–2500 µs`，周期 `20 ms`。

## 类型（可配置）

`servo_type_t`：`90` / `180` / `270` / `360` → 角度范围 `0 … type`，默认居中。

## 时间参数

```c
Servo_writeTimed(servo, angle, duration_ms);
// duration_ms == 0 → 立即到位
// duration_ms  > 0 → ease-out cubic 异步插值（~10ms 步进）
```

银行 API：`DeepDogServoSetAngle(index, angle, duration_ms)`。

## 文件

| 文件 | 职责 |
|------|------|
| `Servo.h` / `Servo.c` | MCPWM 单路 + 插值 |
| `servo_config.h` | GPIO / 脉宽常量 |
| `servo_control.h` / `.cc` | 两路银行 |
| `servo_mcp.cc` | `self.servo.*` MCP |
| MQTT | `mqtt/modules/servo_mqtt.*` |

## 开关

- `config.h`：`DEEP_DOG_EXT_PIN_MODE = DEEP_DOG_EXT_PIN_PWM`（默认）
- `board_features.h`：`DEEP_DOG_SERVO_ENABLE=1`（默认）；`GIMBAL_ENABLE=0`

协议与前端步骤：[swrs/mqtt/modules/10-servo.md](../swrs/mqtt/modules/10-servo.md)。
