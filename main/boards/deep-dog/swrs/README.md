# deep-dog SWRS

Deep-Dog 板级**需求与路线图**唯一入口。

| 项 | 说明 |
|----|------|
| 硬件 | 四足 + OV3660 等（见板级 `config.h`）；可裁剪为非狗全功能模块板 |
| 代码 | 优先 `main/boards/deep-dog/` |
| **权威顺序** | [ROADMAP.md](./ROADMAP.md) |
| **MQTT 契约** | [mqtt/](./mqtt/)（YAML 为字段真源） |

## 定位

**可裁剪全功能模块板**：狗控是可选模块。新项目非机器狗时关 `capabilities.dog`，可保留视觉 / IMU / LED / 云台 / 触摸 / 手柄等（见 [M01](./mqtt/M01-board-mqtt-protocol.md)）。

## 下一步

视觉轨固件侧以 ROADMAP 为准；协议侧 **M01** 已定稿，实现从 **V-C03** MQTT 客户端起。

## 目录

| 路径 | 内容 |
|------|------|
| [ROADMAP.md](./ROADMAP.md) | 可追溯总表、依赖、验收指针 |
| [mqtt/](./mqtt/) | 板级 MQTT 协议（M01 + YAML） |
| [dog/](./dog/) | 四足运动计划（单文件 DEVELOPMENT_PLAN） |
| [vision/](./vision/) | HTTP 服务器 / Immich / MediaMTX 客户端 / Kiosk |
| [vision/infra.md](./vision/infra.md) | MediaMTX、EMQX、Immich 地址（无明文密钥） |

## 交付序号（摘要）

| ID | 主题 | 状态 |
|----|------|------|
| D1～D9, D11～D13 | 运动域 | [dog/DEVELOPMENT_PLAN](./dog/DEVELOPMENT_PLAN.md) |
| V-S01～S06 | HTTP 狗控 / MJPEG / 人脸 / Immich / VGA | 主体 ✅ |
| V-C01～C02 | MediaMTX 验收 + 设备推流 | C02 固件已实现 |
| **M01** | 板级 MQTT 契约 | **文档 ✅** |
| V-C03～C04 | MQTT 推流 / 云台 | 待办（协议见 M01） |
| V-P01 | Kiosk | 更后 |

## 约定

- 未写入 ROADMAP / 对应 SWRS 的需求不要扩大改动面。
- MQTT 字段以 [mqtt/protocol/deep-dog-mqtt.yml](./mqtt/protocol/deep-dog-mqtt.yml) 为准。
- 禁止将 Immich API Key / 密码写入本仓库。
