# CAN 通信模块 (CAN Communication Module)

## 功能概述

本目录提供基于 **ESP-IDF TWAI（CAN 2.0）** 的轻量封装 `TwaiCAN`，全局实例为 **`ESP32Can`**。  
**deep-dog** 上所有电机指令与反馈均走 **扩展帧**（29 位 ID），由 **`../motor/protocol_motor`** 组帧，经 `ESP32Can.writeFrame` 发送；接收在板级 `CanRxTask` 中读帧并交给 `DeepMotor::processCanFrame`。

---

## 文件说明

| 文件 | 说明 |
|------|------|
| `ESP32-TWAI-CAN.hpp` / `ESP32-TWAI-CAN.cpp` | `TwaiCAN`：`begin`/`end`、`writeFrame`/`readFrame`、错误计数、`recover`/`restart` |
| `README.md` | 本文档 |

`typedef twai_message_t CanFrame`（与 ESP-IDF 一致）。

---

## deep-dog 板级配置（`../config.h`）

| 项 | 当前典型值 |
|----|------------|
| 引脚 | `can/can_config.h`：`CAN_TX_GPIO`←`EXT_PIN_A`，`CAN_RX_GPIO`←`EXT_PIN_B`（需 `DEEP_DOG_EXT_PIN_MODE=CAN`） |
| 波特率 | `InitializeCan()` 中使用 **`convertSpeed(1000)`** → **1 Mbps**（须与全部电机一致） |
| 队列 | `setTxQueueSize(64)` / `setRxQueueSize(64)`，`begin(..., 64, 64)`（连续行走突发 12 帧更稳） |

**硬件**：ESP32 GPIO 必须经过 **CAN 收发器**（如 TJA1051、SN65HVD230）接 CANH/CANL；**共地**；总线两端 **120Ω** 终端（按线长与拓扑调整）。

---

## 主要 API

### 初始化（与板级一致）

```cpp
ESP32Can.setTxQueueSize(10);
ESP32Can.setRxQueueSize(10);
bool ok = ESP32Can.begin(
    ESP32Can.convertSpeed(1000),
    (int8_t)CAN_TX_GPIO,
    (int8_t)CAN_RX_GPIO,
    64,   // TX 队列长度
    64    // RX 队列长度
);
```

`begin()` 内部使用 `twai_driver_install` + `twai_start`，模式为 **`TWAI_MODE_NORMAL`**（需总线上有节点应答 ACK，见下文）。

可选：传入自定义 `twai_general_config_t*` / `twai_timing_config_t*` / `twai_filter_config_t*`（最后一个参数默认接受全部）。

### 发送 / 接收

```cpp
CanFrame tx;
// ... 填充 identifier / extd / data_length_code / data[]
ESP32Can.writeFrame(tx, timeout_ms);  // 内部 twai_transmit，超时单位 ms

CanFrame rx;
ESP32Can.readFrame(rx, timeout_ms);   // twai_receive
```

电机协议层统一超时见 **`../motor/protocol_motor.h`** 中的 **`MOTOR_CAN_TIMEOUT_MS`**。

---

## 与电机协议的关系

- **不要**在业务层随意拼 11 位标准帧充当电机指令；本项目电机使用 **29 位扩展 ID**，格式由 `MotorProtocol::buildCanId` 等实现。
- **发送**：`MotorProtocol::sendCanFrame` → `ESP_LOGD` 打印帧（默认不刷屏）→ `ESP32Can.writeFrame`。
- **接收**：扩展帧 + 命令类型为反馈/版本时由 `DeepMotor::processCanFrame` 解析。

---

## 空总线 / 台架注意事项（必读）

在 **标准 NORMAL 模式**下，CAN 数据帧需在 ACK 槽被**至少一个其它节点**应答。若总线上**没有任何电机或分析仪**：

- 发送会累积错误，严重时进入 **Bus-Off**；
- 表现为先发少量帧「偶尔成功」、随后超时，与「第几台电机」无必然关系。

**对策**：接上真实从站；或使用 USB-CAN 等提供 ACK；若仅测 MCU 发帧逻辑，需在驱动层使用 **自测/环回类模式**（需自行改 `twai_general_config_t.mode`，当前仓库默认未开启）。

---

## CAN 帧结构（别名）

```cpp
// CanFrame 即 twai_message_t
uint32_t identifier;       // 标准 11 位或扩展 29 位
uint8_t  extd;             // 1 = 扩展帧
uint8_t  data_length_code;   // 0–8
uint8_t  data[8];
```

---

## 波特率

通过 `ESP32Can.convertSpeed(kbps)` 选择枚举，常用 **1000**（1 Mbps）。总线上所有节点必须一致。

---

## 错误与调试

- `ESP32Can.txFailedCounter()` / `rxErrorCounter()` / `busErrCounter()` / `canState()` 等可用于排查总线异常。
- `recover()`：在 **Bus-Off** 等状态下尝试恢复（见 `ESP32-TWAI-CAN.cpp`）。
- 引脚、波特率、终端电阻不匹配是最常见硬件问题。

---

## 在 deep-dog 中的角色

- **唯一电机总线**：12 个关节电机共享一路 CAN；`../motor` 独占发送路径，板级 `CanRxTask` 循环 `readFrame` 驱动反馈更新。
- **`../leg`、`../dog`** 不直接包含本目录头文件；它们通过 `DeepMotor` / `MotorProtocol` 间接使用 `ESP32Can`。

---

## 相关文档

- `../motor/README.md` — 协议与 `DeepMotor`。
- `../config.h` + `can/can_config.h` — 引出脚成对模式为 CAN 时映射 TX/RX；总开关 `DEEP_DOG_CAN_ENABLE`。
- `../dog/README.md` — 整机与 MCP。
- [`../swrs/mqtt/modules/12-can.md`](../swrs/mqtt/modules/12-can.md) — MQTT `can/*` 透传（网页帧表）。
