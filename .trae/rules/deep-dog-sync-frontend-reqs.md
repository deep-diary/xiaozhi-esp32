---
description: deep-dog MQTT/板级契约变更后，同步更新 deep-trace 前后端 IoT 需求文档
globs: main/boards/deep-dog/docs/requirements/**/*,main/boards/deep-dog/mqtt/**/*,main/boards/deep-dog/config.h,main/boards/deep-dog/board_features.h,main/boards/deep-dog/FEATURE_FLAGS.md
alwaysApply: false
---

# deep-dog → deep-trace 前后端需求同步

设备端、前端、后端分属不同路径。改动 deep-dog 的 MQTT 契约、`capabilities`、入口卡语义、引出脚模式或**设备配对/绑定**后，**必须**同步更新 deep-trace 需求，不能只改固件文档。

## 路径解析（**禁止写死**）

deep-trace 是本仓库**外部**仓库，根路径随机器不同（macOS / Windows / 多克隆）。动手前**先解析**，
**不要**在规则、文档或代码里写死任何一台机器的绝对路径（含 `/Volumes/...`、`D:\...`）。

**解析优先级**（命中即停）：

1. **本机配置** `.trae/paths.local.json`（**本机专属、不入库**）；模板 `.trae/paths.local.example.json`：
   ```json
   { "repoRoots": { "deep_trace": "<deep-trace 仓根>", "deep_dog": "<本仓库根，可省略>" } }
   ```
2. 环境变量 `DEEP_TRACE_ROOT`（无配置文件时的兜底）
3. **候选表探测**：逐个判断该路径是否存在；命中多个时取 `git -C <path> log -1` 提交时间最新者：

   | 角色 | 机器 | 候选根 |
   |------|------|--------|
   | deep-trace 仓根 | Windows | `D:\working\test-platform\deep-trace-com` |
   | deep-trace 仓根 | macOS | `/Volumes/MacExtStorage/projects/deep-trace` |
   | 设备仓根（本仓库） | 任意 | 规则文件所在仓库根（`git rev-parse --show-toplevel`），**无需探测** |

4. 全部解析失败 → **停下来问用户**（要路径或要其补 `.trae/paths.local.json`）；
   不要猜、不要凭记忆写死、不要新建目录。

> **同仓库内引用用相对路径**；**跨仓引用必须用解析后的绝对路径**（对方工作区看不到相对路径）。
> 新机器接入：复制 `.trae/paths.local.example.json` → `.trae/paths.local.json` 并填本机路径即可，
> 无需改本规则（不同电脑可各自指定自己的前端路径）。

## 角色 → 相对路径（拼在解析出的仓根下）

| 角色 | 相对路径 |
|------|----------|
| 设备契约 / docs/requirements | `<deep_dog>/main/boards/deep-dog/docs/requirements/` |
| 设备 MQTT 实现 | `<deep_dog>/main/boards/deep-dog/mqtt/` |
| 前端代码 | `<deep_trace>/frontend/` |
| 后端代码 | `<deep_trace>/backend/` |
| 需求看板（FE/BE 共用） | `<deep_trace>/docs/requirements/features/iot/`（模块详情在 `…/iot/modules/<moduleId>/`） |

**禁止**把 deep-dog IoT 需求写到 DeepDiary 下的 DeepWeb / DeepServer / docs/req。

## 目标目录与同步表

需求根：`<deep_trace>/docs/requirements/features/iot/`

| 变更类型 | 更新位置 |
|----------|----------|
| Hub 页头 / 入口卡显隐 | `hub/`（如 130、145） |
| 添加设备 UX（配对码 / 本地调试） | `hub/`（140 调试；**142** 正式配对码） |
| 绑定 API / 配对入库 / Device 模型 | `iot/` 根横切（**143**，对齐 30）+ `<deep_trace>/backend/devices/README.md` 链接 |
| 模块详情字段 / Topic | `modules/<moduleId>/` |
| 配对 MQTT 契约 | 设备 `docs/requirements/mqtt`（如 `modules/00-pairing.md` + YAML）+ 前端 142 + 后端 143 **三处对齐** |
| 索引与路线图 | `iot/README.md`、必要时 `docs/requirements/README.md` |

编号全局唯一、步长 10；文件夹只表归属。

## 强制约定

1. **跨仓引用用解析后的绝对路径**（把上表的 `<deep_dog>` / `<deep_trace>` 换成实际解析结果），例如：
   - `<deep_dog>/main/boards/deep-dog/docs/requirements/mqtt/protocol/deep-dog-mqtt.yml`
   - `<deep_dog>/main/boards/deep-dog/docs/requirements/mqtt/modules/00-pairing.md`
   - `<deep_dog>/main/boards/deep-dog/docs/requirements/mqtt/modules/01-device.md`
   - `<deep_trace>/frontend/src/iot/homeDevices.js`
   - `<deep_trace>/backend/devices/`
2. 权威契约以固件 YAML / 模块 md 为准；前端需求只写**展示与导航行为**；后端需求写 **API / 模型 / MQTT worker**，勿整份复制 YAML。
3. deep-dog Hub 以 MQTT `device/info.ext_pins`（成对 mode）为真源；edge YAML `io_pins`（REQ-IOT-70）并存，Hub 卡片不靠 YAML 做显隐。
4. 稳定 `device_id` = STA MAC 紧凑串（小写无冒号）；用户添加设备主路径为 **6 位配对码**（REQ-IOT-142/143），勿要求手输 MAC。
5. 同步完成后，可附「给前端 / 后端 / 固件 Agent 的实现提示词」；提示词中的路径同样用**解析后的绝对路径**，并注明该路径属于哪台机器（macOS / Windows）。

## 自检

- [ ] 已解析出 `<deep_trace>`（本机配置 / 环境变量 / 候选表），未使用写死路径；解析不出时已向用户要路径
- [ ] 对应 `hub/`、`modules/` 或根横切（143）需求已更新或新建
- [ ] `iot/README.md`（及总看板若需要）索引已挂上
- [ ] 配对相关变更时：设备 docs/requirements + 142 + 143 字段/Topic 一致
- [ ] 样例 / capabilities / `device_id` 语义与当前固件约定一致（或注明剖面）
- [ ] 未误写 DeepDiary/DeepServer/DeepWeb
