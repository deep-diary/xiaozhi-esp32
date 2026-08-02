# input · 控制输入（手柄）

Deep-Dog **控制输入域**需求入口：统一手柄快照 → Hub → App；与 MQTT 网页契约解耦。

| 项 | 说明 |
|----|------|
| 路线图 | [ROADMAP](../ROADMAP.md) · **I-HANDLE** |
| MQTT 契约 | [modules/11-handle](../mqtt/modules/11-handle.md) · [YAML](../mqtt/protocol/deep-dog-mqtt.yml) |
| 固件 | [`handle/`](../../handle/) + [`handle_mqtt`](../../mqtt/modules/handle_mqtt.h) |
| 镜像分层 | [touch_btn](../../touch_btn/)（Hub + Dispatcher + App） |
| 本轮状态 | **日常：关板载 BT + PC 桥（含 Xbox output 震）**；板载 BT 已验、瘦剖面可再开；keymap(I08a) 已落地 |

## 目标

- **双源并列（v0.1）**
  - 板载：**Bluepad32 + BTstack + Xbox BLE**（`source: bt`）— 固件宏 `DEEP_DOG_HANDLE_BT_ENABLE` 默认关
  - 远端：PC Python 读 **PS4 / Xbox** → MQTT `handle/input`（`source: wifi`）— **已实现**
- 驱动 / 桥只产出统一快照（axes / buttons / connected / source），**不含**狗 / 舵机语义。
- 业务经 `HandleApp*` 拆分。

## 文档

| 文档 | 内容 |
|------|------|
| [I01-architecture](./I01-architecture.md) | Hub / Dispatcher / App；与 touch 对照 |
| [I02-source-bluepad32-xbox](./I02-source-bluepad32-xbox.md) | 板载 Xbox；BTstack；输入矩阵；震动；分区 |
| [I03-source-pc-mqtt-bridge](./I03-source-pc-mqtt-bridge.md) | PC→MQTT 桥；**PS4 HID 对照表 + 极性** |
| [I04-apps-mapping](./I04-apps-mapping.md) | App 启用与键位映射（编译期） |
| [I05-mqtt-message-examples](./I05-mqtt-message-examples.md) | **三段报文展开** · 触控板说明 · Xbox/PS4 异同 |
| [I06-touchpad-xy](./I06-touchpad-xy.md) | 触控板 XY + **双点 contacts** |
| [I07-motion-gyro](./I07-motion-gyro.md) | DS4 手柄 gyro/accel（可选 `motion`） |
| [I08-dynamic-key-action-mapping-eval](./I08-dynamic-key-action-mapping-eval.md) | **评估**：运行时按键→动作表 / MQTT+NVS（灯带试点） |
| [I08a-keymap-mqtt-contract-draft](./I08a-keymap-mqtt-contract-draft.md) | **契约拍板**：`profile`=App 类；keymap + retain `handle/keymap` |
| [I09-ds4-output-feedback](./I09-ds4-output-feedback.md) | PC 桥 `output`：DS4 灯震 + Xbox pygame 震 |
| [assets/ps4-hid-map.png](./assets/ps4-hid-map.png) | DualShock 4 实测标注（Linux） |

## 边界

| 域 | 职责 |
|----|------|
| **本目录 `input/`** | 输入架构、双源规格、App 映射、资源预算 |
| **`mqtt/modules/11-handle`** | Topic、字段、前端 Steps、验收 |
| **`touch/`** | 电容三键；不经 handle Hub |
| **`dog/`** | 运控；由 `HandleAppDog` 调用，不读原始 HID |

## 非目标（当前）

- ESP32-S3 上索尼 PS4/PS5 **无线直连**。
- USB HID Host（仅保留 `source: usb` 枚举）。
- 默认不开 Bluepad32/BTstack（`DEEP_DOG_HANDLE_BT_ENABLE=0`）。

## 前端同步

契约变更后同步 deep-trace：

`/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/modules/handle/`

设备端真源：本目录 + [YAML](../mqtt/protocol/deep-dog-mqtt.yml)。
