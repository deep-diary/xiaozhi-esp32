# V-C04 · MQTT 云台控制（更后）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C04** |
| 优先级 | 低（不插队 S04～C02） |
| 依赖 | [C03](./C03-mqtt-stream-control.md) MQTT 客户端 |
| 状态 | 占位；引脚与硬件需 deep-dog 实机确认后再细化 |

## 目标

MQTT 下发 pan/tilt，两路舵机 PWM；软件限位。

## 协议草案

`.../gimbal/cmd`：`absolute|relative` + `pan`/`tilt`/`speed`；`.../gimbal/status` 回报角度。

## 说明

历史 diary-brain 文档中的 GPIO 38/48 与底盘 UART 互斥方案**不直接套用** deep-dog；实现前单独确认空闲脚与供电。可与人脸跟脸（另立）联动，非本切片范围。
