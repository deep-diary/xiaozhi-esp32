# motor · CAN / 单电机控制

Deep-Dog **单电机调试与 CAN 总线**需求入口。四足整机运控仍见 [dog/](../dog/)；手柄通用映射见 [input/I08b](../input/I08b-axis-mapping.md)。

| 项 | 说明 |
|----|------|
| 路线图 | [ROADMAP](../ROADMAP.md) · **MOT** |
| MQTT | [12-can](../mqtt/modules/12-can.md) · [14-motor](../mqtt/modules/14-motor.md) · [YAML](../mqtt/protocol/deep-dog-mqtt.yml) |
| 固件 | [`can/`](../../can/) · [`motor/`](../../motor/) · `can_mqtt` / `motor_mqtt` |
| 联调剖面 | [FEATURE_FLAGS](../../FEATURE_FLAGS.md)「单电机」：`EXT_PIN=CAN` + `CAN` + `MOTOR`（`DOG=0`） |

## 文档

| 文档 | 内容 |
|------|------|
| [01-can-hw-profile](./01-can-hw-profile.md) | GPIO38/48 TWAI、分层 ENABLE、验收 |
| [02-can-mqtt](./02-can-mqtt.md) | CAN 透传 MQTT（对齐 12-can） |
| [03-motor-mqtt](./03-motor-mqtt.md) | 电机控制 MQTT（对齐 14-motor） |
| [04-handle-motor-catalog](./04-handle-motor-catalog.md) | `profile=motor` 动作 catalog 与默认表 |
| [05-analog-axis-sample](./05-analog-axis-sample.md) | 轴归一化样例；框架权威在 I08b |
| [VERIFY](./VERIFY.md) | 实机验收清单 |

## 边界

| 域 | 职责 |
|----|------|
| **本目录** | 单电机联调、CAN 透传、motor catalog |
| **`input/I08b`** | 跨 profile 模拟量 axis mapping 通用契约 |
| **`dog/`** | 四足步态 / 整机；本剖面默认关 |
| **`arm/`** | 机械臂占位；复用 CAN+MOTOR 基础 |

## 非目标（本轮）

- 打开 `DOG_ENABLE` / 完整步态联调
- 机械臂业务逻辑
- 将 `dog` profile 纳入动态轴表（仍走编译期 `HandleAppDog`）
