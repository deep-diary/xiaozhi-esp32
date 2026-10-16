# deep-dog 功能开关与可复用配置

> **与 Deep Thumble（不倒翁）**：多数能力先在本板落地，再按剖面裁剪移植到 [`deep-thumble`](../deep-thumble/)。产品形态与开源板参考见 [`deep-thumble/docs/开源产品板参考-不倒翁.md`](../deep-thumble/docs/开源产品板参考-不倒翁.md)。

同硬件（ESP-SparkBot 系）只改两处即可切换产品形态：

| 文件 | 职责 |
|------|------|
| [`config.h`](./config.h) | 硬件 GPIO + **自由引出脚成对模式** `DEEP_DOG_EXT_PIN_MODE`（CAN 产品链剖面下由剖面顺带派生，见下） |
| [`board_features.h`](./board_features.h) | 顶部 **`DEEP_DOG_PROFILE` 联调剖面**（改这一行）+ 分层 ENABLE：**引出脚外设**（CAN/UART/IO/LED…）→ 产品链 → **与脚位无关** |

**日常最先改的是剖面**（见 [`docs/requirements/config/CFG-03`](./docs/requirements/config/CFG-03-compile-time-profiles.md)）：

| `DEEP_DOG_PROFILE` | CAN | MOTOR | DOG | ARM | 隐含 `EXT_PIN_MODE` | 适用 |
|--------------------|-----|-------|-----|-----|---------------------|------|
| `DEEP_DOG_PROFILE_CAN_COMM`（**默认**） | 1 | 0 | 0 | 0 | `CAN` | CAN 透传/总线联调；**不背电机栈**（省 `DeepMotor` + `can_rx` 4KB 栈） |
| `DEEP_DOG_PROFILE_MOTOR` | 1 | 1 | 0 | 0 | `CAN` | 单电机调试 |
| `DEEP_DOG_PROFILE_DOG` | 1 | 1 | 1 | 0 | `CAN` | 四足运控 |
| `DEEP_DOG_PROFILE_FULL` | 默认值 | 1 | 0 | 0 | **不干预**（沿用 `config.h`） | 等价于"不设剖面"，按下面 `*_ENABLE` 默认值 |

剖面块只做**预设赋值**，下方 `#ifndef` 默认值与钳位逻辑不变，仍可用 `-DDEEP_DOG_MOTOR_ENABLE=1` 单独覆盖。
剖面还会**顺带定掉编译期兜底引出脚模式**（CFG-03 §3.2）：`board_features.h` 里 `#undef` 后重定义
`DEEP_DOG_EXT_PIN_MODE` / `*_MODE_STR`（因为本文件是 `config.h` 在 mode 与 STR 之后才 include 的），
免去"裁 CAN 却忘了改 `config.h` 默认模式 → 首启 apply 失败回滚 `none`"。要手工指定 mode 就选 `PROFILE_FULL`。

> **CAN 收帧不随 MOTOR 关闭而消失**（CFG-03 §3.3）：`can_rx` 任务的门控已从
> `CAN_ENABLE && MOTOR_ENABLE` 放宽为 `CAN_ENABLE`，无电机时任务参数为 `nullptr`，
> 仅跳过"帧 → 电机状态解算"。`CAN_ENABLE=1` 时 tunnel / `status/can` / Bus-Off 自愈**全部保留**。
> 若用 `PROFILE_FULL` + 手工裁掉 `CAN_ENABLE`，则须自查兜底模式：把 `DEEP_DOG_EXT_PIN_MODE` 设为 `NONE`（否则首启 apply 失败回滚）。

**云台 vs 裸舵机（同脚二选一，编译期）**：`board_features.h` 里的 `DEEP_DOG_GIMBAL_ENABLE`（默认 1，`PWM` 生效模式下初始化云台）。不要单独改底层 `Servo.c` 开关；驱动随 `GIMBAL|SERVO` 自动编入。模块侧请 `#include "config.h"`，**禁止**直接 include `board_features.h`（否则 `*_AVAILABLE` 未定义会被当成 0，误关云台）。

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

> **运行时可配置（已落地，CFG-02）**：38/48 模式不再需要重编固件——所有引出脚软件栈全部编入，
> 由 NVS / 配置中心下发 `io.ext_pins.mode` 决定生效模式（**重启生效**，失败自动回滚）。
> `DEEP_DOG_EXT_PIN_MODE` 现在只是**编译期默认/兜底**。详见 [`swrs/config/CFG-02`](./docs/requirements/config/CFG-02-ext-pin-runtime-mode.md)。

## 引出脚外设 ENABLE（`board_features.h`，默认全 1 = 编入）

| 宏 | 生效模式 | 说明 |
|----|----------|------|
| `CAN_ENABLE` / `MOTOR_ENABLE` | `can` | TWAI + DeepMotor（运行时按模式初始化） |
| `UART_ENABLE` | `uart` | 串口 echo |
| `RS485_ENABLE` | `rs485` | 占位 |
| `IO_ENABLE` | `io` | 普通 GPIO 栈（`io_ext/`） |
| `AD_ENABLE` | `ad` | 占位 |
| `LED_ENABLE` | `led` | WS2812 + MQTT/MCP（`led/`）；DIN 默认 38，可在 `config.h` 覆盖 `DEEP_DOG_LED_STRIP_GPIO` |
| `SERVO_ENABLE` / `GIMBAL_ENABLE` | `pwm` | 舵机 / 云台（同脚互斥，`GIMBAL` 优先） |

> CFG-02 后：`*_ENABLE=1` 只表示「编入固件」，**不再要求**与 `EXT_PIN_MODE` 匹配；
> 真正决定初始化哪个栈的是运行时生效模式（`ext_pin/`）。
> 反过来，把某栈 `*_ENABLE` 改回 `0` 仍可裁剪省 flash —— 该模式会自动从
> `device/info.ext_pins.supported_modes` 白名单消失（前端随之禁用、下发返回 `E_UNSUPPORTED_MODE`），
> 所以裁剪只需改 `board_features.h` 一处（白名单是条件编译的）。

**灯带不在 38/48 上时**：在 `config.h` 里 `#define DEEP_DOG_LED_STRIP_GPIO GPIO_NUM_xx` 即可；仍用 `LED_ENABLE=1`，**不要**改用 `IO_ENABLE`。

## 分层 ENABLE（CAN 栈）

```text
EXT_PIN=CAN → CAN_AVAILABLE
CAN_ENABLE → MOTOR_ENABLE → DOG_ENABLE
                          ↘ ARM_ENABLE
```

预处理钳位在 `board_features.h`：缺依赖时强制下层为 0。

`leg/`、`trajectory/` 属于四足步态，随 **DOG** 使用；单电机调试不必开 DOG。

板级初始化（`esp_sparkbot_board.cc`）按层调用：`InitializeCan` →（**`CAN_ENABLE=1` 即建**）`can_rx` 收帧任务 +（`MOTOR_ENABLE=1` 时）`DeepMotor` →（可选）`DogControl` / `Arm` 占位。

> 说明：`can/`、`motor/`、`dog/`、`leg/`、`trajectory/` 的实现 TU 用整文件 `#if DEEP_DOG_*_ENABLE` 门控（`WHOLE_ARCHIVE` 下空 TU 几乎不占 flash）。调用侧（板级初始化 / MCP / HTTP 狗 API / 触摸）同步 `#if`。改开关后普通重编即可，无需改 CMake。

## 联调剖面

> CFG-02 后「剖面」= **运行时生效模式**（`ext_pin/` 决定）。下表 `EXT_PIN` 列即目标 mode；
> 产品级开关（`DOG` / `ARM` / `SERVO` vs `GIMBAL`）仍需编译期决定。

| 剖面 | EXT_PIN | CAN | MOTOR | DOG | ARM | SERVO/GIMBAL | 说明 |
|------|---------|-----|-------|-----|-----|--------------|------|
| **CAN 通讯联调（当前默认）** | `CAN` | 1 | **0** | 0 | 0 | 0 | `PROFILE_CAN_COMM`：TWAI + `can_rx` 收帧 + can MQTT 透传/心跳/自愈；**无电机栈**；GPIO38(TX)/48(RX) |
| 单电机 | `CAN` | 1 | 1 | 0 | 0 | 0 | `PROFILE_MOTOR`：TWAI + DeepMotor + can/motor MQTT + handle motor/轴；GPIO38(TX)/48(RX) |
| 云台 | `PWM` | 0 | 0 | 0 | 0 | 0/1 | 产品 pan/tilt MQTT + 手柄 keymap；GPIO38/48 |
| 舵机调试 | `PWM` | 0 | 0 | 0 | 0 | **1/0** | 2 路裸舵机 MQTT/MCP（与云台互斥） |
| 灯带联调 | `LED` | 0 | 0 | 0 | 0 | 0 | `LED_ENABLE=1`；DIN=`gpio_a`(38) |
| 前端壳 | `NONE` | 0 | 0 | 0 | 0 | 0 | MQTT 设备页联调 |
| 瘦剖面 | `LED` | 0 | 0 | 0 | 0 | 0 | **240²** 采集/人脸/RTSP；§9 640 代码保留 |
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
- **`DEEP_DOG_CAN_HEARTBEAT_MS`**（`can/can_config.h`，**默认 2000**：每 2 s 注入 1 条模拟 RX 心跳帧
  `id=0x1F000000`，仅走 MQTT 透传、**不写总线**，用于区分「透传链路故障」与「总线无报文」；`0` = 关闭）
- **`DEEP_DOG_CAN_HEALTH_CHECK_MS`**（`can/can_config.h`，**默认 1000**：轮询 TWAI 状态，
  Bus-Off 自动 `recover()` + `restart()`；次数上报 `status/can.bus_off_count` / `recover_count`）
- CAN 透传默认值：`tunnel=true`、`allow_tx=true`、`ext_only=false`、`max_hz=100`（`mqtt/modules/can_mqtt.h`
  成员初值，**联调剖面**；`allow_tx=true` 表示网页可注入原始帧，`ext_only=false` 表示标准帧也上报，
  量产/正式版本须改回 `allow_tx=false`、`ext_only=true`）

引出脚外设 ENABLE 见上表（`CAN`/`UART`/`IO`/`LED` 等）；**灯带只用 `LED_ENABLE`，不用 `IO_ENABLE`**。

## MQTT 上报

`device/info`（retain）含：

```json
"ext_pins": {
  "mode": "can", "gpio_a": 38, "gpio_b": 48,
  "supported_modes": ["can","pwm","led","uart","rs485","io","ad","none"],
  "pending_mode": "pwm", "apply_policy": "restart"
},
"capabilities": { "can", "motor", "dog", "arm", "uart", "servo", "gimbal", "led", ... }
```

前端：`ext_pins.mode` 选总线类页面；`capabilities.motor` vs `dog` 区分电机调试与四足。编译默认 `config.h` 为 **`EXT_PIN=CAN`**（单电机 CAN 联调，38=TX/48=RX），可运行时改（`pending_mode` → 重启生效）。MQTT 联调默认 broker 见 [infra.md](./docs/requirements/vision/infra.md)（`192.168.3.73:1883（本机 EMQX；见 infra.md）`）。

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
