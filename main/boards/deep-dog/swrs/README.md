# deep-dog SWRS

Deep-Dog 板级**需求与路线图**唯一入口。

| 项 | 说明 |
|----|------|
| 硬件 | 四足 + OV3660 等（见板级 `config.h`）；可裁剪为非狗全功能模块板 |
| 代码 | 优先 `main/boards/deep-dog/` |
| **权威顺序** | [ROADMAP.md](./ROADMAP.md) |
| **MQTT** | [mqtt/](./mqtt/)（M01 总览 + modules 前端详情页 + YAML） |
| **控制输入** | [input/](./input/)（手柄双源；I-HANDLE；I08b 轴映射） |
| **电机 / CAN** | [motor/](./motor/)（单电机联调；MOT） |

## 定位

**可裁剪全功能模块板**：狗控可选。前端设备页为**入口卡**，点进**模块独立页**（见 [mqtt/frontend/00-device-page](./mqtt/frontend/00-device-page.md)）。

不倒翁产品（`deep-thumble`）以本板为功能枢纽、再移植落地；开源产品板参考见 [开源产品板参考-不倒翁.md](../../deep-thumble/docs/开源产品板参考-不倒翁.md)。

## 下一步

视觉轨以 ROADMAP 为准；MQTT 前端规格见 [mqtt/README](./mqtt/README.md)；固件实现从 [modules/02-stream](./mqtt/modules/02-stream.md)（V-C03）起。

## 目录

| 路径 | 内容 |
|------|------|
| [ROADMAP.md](./ROADMAP.md) | 可追溯总表 |
| [mqtt/](./mqtt/) | MQTT：M01、frontend、modules、YAML |
| [input/](./input/) | 控制输入（手柄双源：Bluepad32 / PC MQTT 桥） |
| [motor/](./motor/) | CAN / 单电机调试（MOT；手柄 catalog 见 04） |
| [dog/](./dog/) | 四足运动 |
| [vision/](./vision/) | HTTP / Immich / MediaMTX / Kiosk |
| [vision/infra.md](./vision/infra.md) | MediaMTX、EMQX、Immich（无明文密钥） |

## 交付序号（摘要）

| ID | 主题 | 状态 |
|----|------|------|
| D1～D9, D11～D13 | 运动域 | [dog/DEVELOPMENT_PLAN](./dog/DEVELOPMENT_PLAN.md) |
| V-S01～S06 | HTTP 视觉 Server | 主体 ✅ |
| V-C01～C02 | MediaMTX + 设备推流 | C02 固件已实现 |
| **M01** + modules | MQTT 契约与前端规格 | **文档 ✅** |
| **I-HANDLE** | 手柄双源（Xbox BT / PC→MQTT） | [input/](./input/) **文档 ✅**；wifi/keymap 已落地 |
| **MOT** | CAN + 单电机 + 手柄 motor/轴 | [motor/](./motor/) · I08b | 本轮落地 |
| V-C03 | 推流 MQTT | [modules/02-stream](./mqtt/modules/02-stream.md) 待办 |
| V-C04 | 云台 MQTT | [modules/09-gimbal](./mqtt/modules/09-gimbal.md) 更后 |
| V-P01 | Kiosk | 更后 |

## 约定

- 未写入 ROADMAP / SWRS 的需求不要扩大改动面。
- MQTT 字段以 [mqtt/protocol/deep-dog-mqtt.yml](./mqtt/protocol/deep-dog-mqtt.yml) 为准。
- 禁止将 Immich API Key / 密码写入本仓库。
