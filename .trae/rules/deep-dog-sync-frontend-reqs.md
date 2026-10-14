---
description: deep-dog MQTT/板级契约变更后，同步更新 deep-trace 前后端 IoT 需求文档
globs: main/boards/deep-dog/swrs/**/*,main/boards/deep-dog/mqtt/**/*,main/boards/deep-dog/config.h,main/boards/deep-dog/board_features.h,main/boards/deep-dog/FEATURE_FLAGS.md
alwaysApply: false
---

# deep-dog → deep-trace 前后端需求同步

设备端、前端、后端分属不同路径。改动 deep-dog 的 MQTT 契约、`capabilities`、入口卡语义、引出脚模式或**设备配对/绑定**后，**必须**同步更新 deep-trace 需求，不能只改固件文档。

## 仓库路径（绝对路径，禁止写到 DeepDiary 前后端）

| 角色 | 绝对路径 |
|------|----------|
| 设备契约 / swrs | `/Volumes/MacExtStorage/projects/DeepDiary/xiaozhi-esp32/main/boards/deep-dog/swrs/` |
| 设备 MQTT 实现 | `/Volumes/MacExtStorage/projects/DeepDiary/xiaozhi-esp32/main/boards/deep-dog/mqtt/` |
| 前端代码 | `/Volumes/MacExtStorage/projects/deep-trace/frontend/` |
| 后端代码 | `/Volumes/MacExtStorage/projects/deep-trace/backend/` |
| 需求文档（FE/BE 共用看板） | `/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/` |

**禁止**把 deep-dog IoT 需求写到 DeepDiary 下的 DeepWeb / DeepServer / docs/req。

## 目标目录与同步表

需求根：`/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/`

| 变更类型 | 更新位置 |
|----------|----------|
| Hub 页头 / 入口卡显隐 | `hub/`（如 130、145） |
| 添加设备 UX（配对码 / 本地调试） | `hub/`（140 调试；**142** 正式配对码） |
| 绑定 API / 配对入库 / Device 模型 | `iot/` 根横切（**143**，对齐 30）+ `/Volumes/MacExtStorage/projects/deep-trace/backend/devices/README.md` 链接 |
| 模块详情字段 / Topic | `modules/<moduleId>/` |
| 配对 MQTT 契约 | 设备 `swrs/mqtt`（如 `modules/00-pairing.md` + YAML）+ 前端 142 + 后端 143 **三处对齐** |
| 索引与路线图 | `iot/README.md`、必要时 `docs/requirements/README.md` |

编号全局唯一、步长 10；文件夹只表归属。

## 强制约定

1. **引用跨仓文件一律用绝对路径**（对方工作区看不到相对路径），例如：
   - `/Volumes/MacExtStorage/projects/DeepDiary/xiaozhi-esp32/main/boards/deep-dog/swrs/mqtt/protocol/deep-dog-mqtt.yml`
   - `/Volumes/MacExtStorage/projects/DeepDiary/xiaozhi-esp32/main/boards/deep-dog/swrs/mqtt/modules/00-pairing.md`
   - `/Volumes/MacExtStorage/projects/DeepDiary/xiaozhi-esp32/main/boards/deep-dog/swrs/mqtt/modules/01-device.md`
   - `/Volumes/MacExtStorage/projects/deep-trace/frontend/src/iot/homeDevices.js`
   - `/Volumes/MacExtStorage/projects/deep-trace/backend/devices/`
2. 权威契约以固件 YAML / 模块 md 为准；前端需求只写**展示与导航行为**；后端需求写 **API / 模型 / MQTT worker**，勿整份复制 YAML。
3. deep-dog Hub 以 MQTT `device/info.ext_pins`（成对 mode）为真源；edge YAML `io_pins`（REQ-IOT-70）并存，Hub 卡片不靠 YAML 做显隐。
4. 稳定 `device_id` = STA MAC 紧凑串（小写无冒号）；用户添加设备主路径为 **6 位配对码**（REQ-IOT-142/143），勿要求手输 MAC。
5. 同步完成后，可附「给前端 / 后端 / 固件 Agent 的实现提示词」，路径一律绝对路径。

## 自检

- [ ] 对应 `hub/`、`modules/` 或根横切（143）需求已更新或新建
- [ ] `iot/README.md`（及总看板若需要）索引已挂上
- [ ] 配对相关变更时：设备 swrs + 142 + 143 字段/Topic 一致
- [ ] 样例 / capabilities / `device_id` 语义与当前固件约定一致（或注明剖面）
- [ ] 未误写 DeepDiary/DeepServer/DeepWeb
