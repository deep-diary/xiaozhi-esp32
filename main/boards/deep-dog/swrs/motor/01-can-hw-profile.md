# MOT-01 · CAN 硬件与联调剖面

| 项 | 内容 |
|----|------|
| ID | MOT-01 |
| 状态 | 规格拍板；固件随本剖面启用 |
| 依赖 | — |
| 对照 | [FEATURE_FLAGS](../../FEATURE_FLAGS.md) · [can/README](../../can/README.md) |

## 目标

自由引出脚 **GPIO38 / GPIO48** 配成 CAN（TWAI），并使能电机栈，作为单电机调试与后续机械臂的基础。

## 引脚与模式

| 项 | 值 |
|----|-----|
| `DEEP_DOG_EXT_PIN_A_GPIO` | 38（CAN TX） |
| `DEEP_DOG_EXT_PIN_B_GPIO` | 48（CAN RX） |
| `DEEP_DOG_EXT_PIN_MODE` | `DEEP_DOG_EXT_PIN_CAN` |
| `device/info.ext_pins.mode` | `"can"` |

模式互斥：`CAN` 时 PWM/云台/裸舵机不可用（`PWM_AVAILABLE=0`）。

## 分层 ENABLE

```text
EXT_PIN=CAN → CAN_AVAILABLE
CAN_ENABLE=1 → MOTOR_ENABLE=1 → DOG_ENABLE=0（本剖面）
                              ↘ ARM_ENABLE=0（占位）
```

本分支默认剖面（单电机）：

| 宏 | 值 |
|----|-----|
| `DEEP_DOG_CAN_ENABLE` | 1 |
| `DEEP_DOG_MOTOR_ENABLE` | 1 |
| `DEEP_DOG_DOG_ENABLE` | 0 |
| `DEEP_DOG_GIMBAL_ENABLE` / `SERVO` | 0（由 PWM 不可用钳位） |

板级初始化：`InitializeCan` → `DeepMotor` + RX 任务。波特率 **1 Mbps**（见 `can/README`）。

## 验收

- [ ] `device/info`：`ext_pins.mode=can`，`gpio_a=38`，`gpio_b=48`
- [ ] `capabilities.can === true`，`capabilities.motor === true`，`dog === false`
- [ ] `capabilities.gimbal/servo === false`（本剖面）
- [ ] 串口日志可见 CAN / DeepMotor 初始化成功
