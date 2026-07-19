# 04 · MQTT 云台控制

| 项 | 内容 |
|----|------|
| 优先级 | P1 |
| 依赖 | [00 EMQX](./00-shared-infra.md)；建议在 [03](./03-mqtt-stream-control.md) 的 MQTT 客户端就绪后做 |
| 代码落点 | `main/boards/diary-brain/`（删底盘 UART → PWM 驱动 + MQTT 处理） |
| 验收 | 无底盘 UART；MQTT 可驱动两轴舵机云台 |

## 1. 背景

用两路 PWM 驱动云台（水平 / 俯仰），引脚固定为 **GPIO 38、48**。

原始 `esp-sparkbot` / 当前 `diary_brain_board.cc` 把这两脚用作底盘 UART（`UART_ECHO_TXD/RXD`）及履带 MCP 工具。**Diary Brain 暂不需要底盘**，因此：

1. **删除**板内全部底盘 UART 及相关逻辑；
2. 将 GPIO 38 / 48 **专用于**云台舵机 PWM。

## 2. 目标

- 板内不再初始化、使用底盘串口；无底盘 MCP 工具。
- MQTT 下发云台相对/绝对角度，设备输出两路舵机 PWM。
- 与视频观看联动：网页边看 HLS 边调云台（前端在 [07] 或独立调试页）。

## 3. 范围

**包含**

- 删除底盘 UART 驱动、宏、`InitializeEchoUart`、`SendUartMessage`、底盘相关 MCP tools
- `config.h` 中 38/48 改为云台 PWM 引脚定义（并注释用途）
- 两轴舵机 PWM（典型 50Hz，脉宽 0.5–2.5ms，以实际舵机为准）
- MQTT cmd / status
- 软件限位与速度限制，避免猛转

**不包含**

- 履带底盘 / 灯光串口协议（本板不做；若将来需要，另开需求并改引脚方案）
- 视觉伺服自动追踪人脸（可列为后续增强）
- 多路云台 / 带反馈电位器闭环（P1 开环即可）

## 4. 引脚与清理（已拍板）

| 引脚 | 原用途（sparkbot / 现状） | Diary Brain 用途 |
|------|---------------------------|------------------|
| GPIO 38 | `UART_ECHO_TXD`（底盘） | **云台 PWM 轴 A**（建议 pan） |
| GPIO 48 | `UART_ECHO_RXD`（底盘） | **云台 PWM 轴 B**（建议 tilt） |

**须删除的板内逻辑（实现时对照清单）**

- `config.h`：`UART_ECHO_*`、`ECHO_UART_*`、`BUF_SIZE`（若仅服务底盘）、`MOTOR_SPEED_*`（若仅服务底盘）等
- `diary_brain_board.cc`：`InitializeEchoUart`、`SendUartMessage`、构造函数中的 UART 初始化调用
- MCP：`self.chassis.*` 全部工具（前进/后退/转向/跳舞/灯光等）
- 其它仅服务底盘 UART 的成员与枚举（如仅被底盘使用的 `light_mode_` 串口切换逻辑）

> 灯光若仍依赖底盘串口协议，一并移除；屏幕背光等与 UART 无关的能力保留。

## 5. 协议草案

**命令** `.../gimbal/cmd`

```json
{
  "mode": "absolute" | "relative",
  "pan": 0,
  "tilt": 0,
  "speed": 50,
  "ts": 1710000000
}
```

- `pan` / `tilt`：`absolute` 时为角度（度，中位约定实现时写明）；`relative` 时为增量。
- `speed`：0–100，映射到步进间隔。

**状态** `.../gimbal/status`

```json
{ "pan": 10, "tilt": -5, "ts": 1710000000 }
```

可选快捷命令：`{ "action": "center" }` 回中。

## 6. 功能需求

| ID | 需求 | 说明 |
|----|------|------|
| GIM-00 | 移除底盘 UART | 删除 §4 清单内全部 UART/底盘逻辑；编译通过且无残留引用 |
| GIM-01 | PWM 输出 | GPIO 38 / 48 两路 LEDC PWM |
| GIM-02 | MQTT 控制 | 解析 cmd，驱动舵机，上报 status |
| GIM-03 | 限位 | 超出软限位则钳位并在 status/error 中体现 |
| GIM-04 | 上电姿态 | 默认回中（更安全）；若改为保持上次角度需 NVS 并文档说明 |
| GIM-05 | 引脚注释 | `config.h` 明确 `GIMBAL_PAN_GPIO` / `GIMBAL_TILT_GPIO`，不再出现 `UART_ECHO_*` |

## 7. 验收标准

- [ ] 板内无底盘 UART 初始化与 `self.chassis.*` MCP；相关宏已删除或不再编译
- [ ] GPIO 38/48 仅作为云台 PWM 使用，`config.h` 注释清晰
- [ ] MQTT 调整 pan/tilt，目视云台动作正确、无持续抖动
- [ ] 限位内安全；错误 payload 不导致 PWM 跑飞

## 8. 开放问题

1. 舵机型号与角度中点、供电（5V 舵机与板子供电是否隔离）？
2. pan/tilt 与 GPIO 38/48 的最终对应关系（上文为建议，接线后确认）？
3. 是否需要与人脸检测联动「自动把人脸移到画面中心」（P2）？
