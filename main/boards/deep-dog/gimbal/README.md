# Gimbal（云台）

双轴 pan / tilt 产品封装，底层复用 [`servo/`](../servo/) MCPWM。

## 默认剖面

| 宏 | 默认 | 说明 |
|----|------|------|
| `DEEP_DOG_GIMBAL_ENABLE` | **1** | 产品云台 |
| `DEEP_DOG_SERVO_ENABLE` | **0** | 裸舵机调试；与云台互斥（同 EXT A/B） |

调试裸舵机时改回 `SERVO=1` / `GIMBAL=0`。

## API 一览

| 类别 | 函数 |
|------|------|
| 生命周期 | `Gimbal_init` / `deinit` / `isInitialized` |
| 绝对角 | `Gimbal_setAngles`（按当前轴速度插值） |
| 相对 | `Gimbal_nudgeLeft/Right/Up/Down`；`Gimbal_moveRelative` |
| Jog / 停 | `Gimbal_startJog` / `stopJog` / `stop` |
| **回中复位** | `Gimbal_home`（中心角 + 默认速度/步进；清 jog/模拟） |
| 速度 | `Gimbal_setPanSpeed` / `setTiltSpeed`；`panSpeedUp/Down`；`tiltSpeedUp/Down` |
| 状态 | `Gimbal_getStatus` |

速度单位 °/s；点按步进角 `DEEP_DOG_GIMBAL_STEP_DEG`；速度档步进见 `gimbal_config.h`。

## MQTT / 手柄

- Topic：`gimbal/cmd` · `gimbal/status`（见 `swrs/mqtt/modules/09-gimbal.md`）
- 按键绑定：`handle/keymap`，`profile=gimbal`（I08a press/hold）

## 出厂手柄建议（profile=gimbal）

| 键 | press | hold |
|----|-------|------|
| a | gimbal.left | gimbal.left |
| b | gimbal.right | gimbal.right |
| x | gimbal.up | gimbal.up |
| y | gimbal.down | gimbal.down |
| dpad | 调速 | — |
| **r3** | **gimbal.home** | none |
| 轴 rx/ry | pan_rate / tilt_rate | — |
| r1 | gimbal.pan_speed_down | none |
