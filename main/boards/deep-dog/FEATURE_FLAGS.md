# deep-dog 功能开关与可复用配置

> **与 Deep Thumble（不倒翁）**：多数能力先在本板落地，再按剖面裁剪移植到 [`deep-thumble`](../deep-thumble/)。产品形态与开源板参考见 [`deep-thumble/docs/开源产品板参考-不倒翁.md`](../deep-thumble/docs/开源产品板参考-不倒翁.md)。

同硬件（ESP-SparkBot 系）只改两处即可切换产品形态：

| 文件 | 职责 |
|------|------|
| [`config.h`](./config.h) | 硬件 GPIO + **自由引出脚成对模式** `DEEP_DOG_EXT_PIN_MODE` |
| [`board_features.h`](./board_features.h) | 分层 ENABLE：**引出脚外设**（CAN/UART/IO/LED…）→ 产品链 → **与脚位无关** |

**只改一个宏开/关云台**：`board_features.h` 里的 `DEEP_DOG_GIMBAL_ENABLE`（需 `EXT_PIN=PWM`）。不要单独改底层 `Servo.c` 开关；驱动随 `GIMBAL|SERVO` 自动编入。模块侧请 `#include "config.h"`，**禁止**直接 include `board_features.h`（否则 `*_AVAILABLE` 未定义会被当成 0，误关云台）。

模块参数（波特率、电机增益、步态等）留在各目录 `*_config.h`，**不要**在模块内写死 GPIO 38/48。

## 引出脚成对模式

`DEEP_DOG_EXT_PIN_A_GPIO` / `B` 默认 38 / 48。

| `DEEP_DOG_EXT_PIN_*` | AVAILABLE | 典型用途 |
|---------------------|-----------|----------|
| `NONE` | — | 前端壳联调 |
| `CAN` | `CAN_AVAILABLE` | TWAI；A=TX B=RX |
| `UART` | `UART_AVAILABLE` | A=TXD B=RXD |
| `RS485` | `RS485_AVAILABLE` | 占位 |
| `PWM` | `PWM_AVAILABLE` | 舵机/云台 pan/tilt |
| `IO` | `IO_AVAILABLE` | **纯 GPIO** 输入/输出（A/B 两路；与 LED 互斥） |
| `AD` | `AD_AVAILABLE` | 占位 |
| `LED` | `LED_AVAILABLE` | **WS2812 灯带**；A=DIN，B 空闲；用 `LED_ENABLE`，**不要**开 `IO_ENABLE` |

同一 `EXT_PIN_MODE` 下 **IO 与 LED 不会同时 AVAILABLE**；灯带不是「IO 的一种用法」，而是独立模式。

## 引出脚外设 ENABLE（`board_features.h`，须 `EXT_PIN_MODE` 匹配）

| 宏 | 须 EXT_PIN | 说明 |
|----|------------|------|
| `CAN_ENABLE` / `MOTOR_ENABLE` | `CAN` | TWAI + DeepMotor |
| `UART_ENABLE` | `UART` | 串口 echo |
| `RS485_ENABLE` | `RS485` | 占位 |
| `IO_ENABLE` | `IO` | 普通 GPIO 栈（`io_ext/`） |
| `AD_ENABLE` | `AD` | 占位 |
| `LED_ENABLE` | `LED` | WS2812 + MQTT/MCP（`led/`）；DIN 默认 38，可在 `config.h` 覆盖 `DEEP_DOG_LED_STRIP_GPIO` |

**灯带不在 38/48 上时**：保持 `EXT_PIN=LED`（MQTT `ext_pins.mode=led`），在 `config.h` 里 `#define DEEP_DOG_LED_STRIP_GPIO GPIO_NUM_xx` 即可；仍用 `LED_ENABLE=1`，**不要**改用 `IO_ENABLE`。

## 分层 ENABLE（CAN 栈）

```text
EXT_PIN=CAN → CAN_AVAILABLE
CAN_ENABLE → MOTOR_ENABLE → DOG_ENABLE
                          ↘ ARM_ENABLE
```

预处理钳位在 `board_features.h`：缺依赖时强制下层为 0。

`leg/`、`trajectory/` 属于四足步态，随 **DOG** 使用；单电机调试不必开 DOG。

板级初始化（`esp_sparkbot_board.cc`）按层调用：`InitializeCan` → `DeepMotor` + RX 任务 →（可选）`DogControl` / `Arm` 占位。

> 说明：`can/`、`motor/`、`dog/`、`leg/`、`trajectory/` 的实现 TU 用整文件 `#if DEEP_DOG_*_ENABLE` 门控（`WHOLE_ARCHIVE` 下空 TU 几乎不占 flash）。调用侧（板级初始化 / MCP / HTTP 狗 API / 触摸）同步 `#if`。改开关后普通重编即可，无需改 CMake。

## 联调剖面

| 剖面 | EXT_PIN | CAN | MOTOR | DOG | ARM | SERVO/GIMBAL | 说明 |
|------|---------|-----|-------|-----|-----|--------------|------|
| **云台（当前默认）** | `PWM` | 0 | 0 | 0 | 0 | **0/1** | 产品 pan/tilt MQTT + 手柄 keymap；GPIO38/48 |
| 舵机调试 | `PWM` | 0 | 0 | 0 | 0 | **1/0** | 2 路裸舵机 MQTT/MCP（与云台互斥） |
| 灯带联调 | `LED` | 0 | 0 | 0 | 0 | 0 | `LED_ENABLE=1`；DIN=`gpio_a`(38) |
| 前端壳 | `NONE` | 0 | 0 | 0 | 0 | 0 | MQTT 设备页联调 |
| 瘦剖面 | `LED` | 0 | 0 | 0 | 0 | 0 | **240²** 采集/人脸/RTSP；§9 640 代码保留 |
| 单电机 | `CAN` | 1 | 1 | 0 | 0 | 0 | TWAI + DeepMotor + can/motor MQTT + handle motor/轴 |
| 四足狗 | `CAN` | 1 | 1 | 1 | 0 | 0 | 完整运控 |
| 机械臂 | `CAN` | 1 | 1 | 0 | 1 | 0 | 占位，实现后启用 |
| UART | `UART` | 0 | 0 | 0 | 0 | 0 | `UART_ENABLE=1` |

非引脚开关（默认可按联调需要改 `board_features.h` / 各 `*_config.h`）：

- `DEEP_DOG_MQTT_ENABLE`（默认 1）
- `DEEP_DOG_VISION_HUB_ENABLE` / `FACE_AI` / `IMU` / `TRACK_MQTT`
- **`DEEP_DOG_FACE_AI_DEFAULT_ENABLED`**（`face_ai_config.h`，**默认 0**：开机不启检测/识别；`face/cmd` 可再开）
- **`DEEP_DOG_VISION_PUSH_AT_BOOT`**（`vision_config.h`，**默认 0**：不开自动推流）
- `DEEP_DOG_HTTP_SERVER_ENABLE`（默认 0）
- `DEEP_DOG_HANDLE_ENABLE`（云台剖面默认 1；瘦剖面可关）

引出脚外设 ENABLE 见上表（`CAN`/`UART`/`IO`/`LED` 等）；**灯带只用 `LED_ENABLE`，不用 `IO_ENABLE`**。

## MQTT 上报

`device/info`（retain）含：

```json
"ext_pins": { "mode": "can", "gpio_a": 38, "gpio_b": 48 },
"capabilities": { "can", "motor", "dog", "arm", "uart", "servo", "gimbal", "led", ... }
```

前端：`ext_pins.mode` 选总线类页面；`capabilities.motor` vs `dog` 区分电机调试与四足。当前默认 `config.h` 为 **`EXT_PIN=PWM`** + `GIMBAL_ENABLE=1`（云台联调）。MQTT 联调默认 broker 见 [infra.md](./swrs/vision/infra.md)（`192.168.3.73:1883（本机 EMQX；见 infra.md）`）。

## 目录与依赖

```text
config.h / board_features.h
    ├─ can/          ← CAN_ENABLE（实现 TU 整文件 #if）
    ├─ motor/        ← MOTOR_ENABLE（整文件 #if）
    ├─ dog/ leg/ trajectory/  ← DOG_ENABLE（整文件 #if）
    ├─ arm/          ← ARM_ENABLE（占位）
    ├─ uart/ rs485/ io_ext/ ad/  ← 对应 EXT 模式（IO=纯 GPIO）
    ├─ servo/ gimbal/ ← PWM 模式
    ├─ led/          ← LED_ENABLE（EXT_PIN=LED；非 IO 栈）
    ├─ mqtt/ vision/ face_ai/ sensor/ http-server/ …
    └─ esp_sparkbot_board.cc 编排
```
