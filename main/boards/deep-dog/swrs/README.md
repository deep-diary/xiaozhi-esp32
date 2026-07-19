# deep-dog SWRS

Deep-Dog 板级**需求与路线图**唯一入口。

| 项 | 说明 |
|----|------|
| 硬件 | 四足 + OV3660 等（见板级 `config.h`） |
| 代码 | 优先 `main/boards/deep-dog/` |
| **权威顺序** | [ROADMAP.md](./ROADMAP.md) |

## 下一步

**V-S04** — [本地人脸数字 ID](./vision/server/S04-local-face-numeric-id.md)（自动建档；不同人不同 ID；**不调 Immich**）

顺序：Server S01→S05 → Client C01→C03 → Product P01。

## 目录

| 路径 | 内容 |
|------|------|
| [ROADMAP.md](./ROADMAP.md) | 可追溯总表、依赖、验收指针 |
| [dog/](./dog/) | 四足运动计划（单文件 DEVELOPMENT_PLAN） |
| [vision/](./vision/) | HTTP 服务器 / Immich / MediaMTX 客户端 / Kiosk |
| [vision/infra.md](./vision/infra.md) | MediaMTX、EMQX、Immich 地址（无明文密钥） |

## 交付序号（摘要）

| ID | 主题 | 状态 |
|----|------|------|
| D1～D9, D11～D13 | 运动域 | [dog/DEVELOPMENT_PLAN](./dog/DEVELOPMENT_PLAN.md) |
| V-S01～S03 | HTTP 狗控 / MJPEG / 人脸框 | 主体 ✅ |
| **V-S04** | 本地数字 ID | **下一步** |
| V-S05 | Immich 真名 | 待办 |
| V-C01～C02 | MediaMTX 验收 + 设备推流（复用 face） | 待办 |
| V-C03～C04 | MQTT 推流 / 云台 | 更后 |
| V-P01 | Kiosk | 更后 |

## 约定

- 未写入 ROADMAP / 对应 SWRS 的需求不要扩大改动面。
- 禁止将 Immich API Key / 密码写入本仓库。
