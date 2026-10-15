# deep-dog ⇄ deep-trace 跨端接口契约（Interop）

本目录是 **设备端（xiaozhi-esp32 / deep-dog）** 与 **云平台端（deep-trace-com：Django 后端 + Vue 前端）**
之间的**跨仓库接口契约基线**。目标是让两端在**不改对方代码**的前提下各自独立开发、并行联调。

> 与 [swrs/README.md](../README.md)（板级需求）和 [swrs/mqtt/README.md](../mqtt/README.md)（板级 MQTT 契约）并列，
> 本目录**只描述"跨端"接口**，不重复板内业务细节。

## 目录内容

| 文档 | 内容 | 主要读者 |
|------|------|----------|
| [00-architecture-review.md](./00-architecture-review.md) | 现状架构审查：问题清单（分级）、优化建议、迁移路线图、风险与验收 | 架构/技术负责人、两端主力 |
| [01-mqtt-contract.md](./01-mqtt-contract.md) | **MQTT 标准接口契约**：命名空间、主题命名规范、统一信封、逐模块主题表、生命周期、版本兼容 | 设备固件、云端 worker、前端 MQTT |
| [02-ws-contract.md](./02-ws-contract.md) | **WebSocket 标准接口契约**：设备 WS-MCP、前端原生 WS、MQTT-over-WS 接入规范、心跳/重连 | 设备固件、前端 |
| [03-http-api-contract.md](./03-http-api-contract.md) | **HTTP/REST 标准接口契约**：设备内置 httpd、设备配对握手、云端 REST API 规范与端点清单 | 设备固件、云端后端、前端 |

## 读者与使用方式（两端独立开发）

```
                     本目录（契约，唯一基线）
                              │
        ┌─────────────────────┴─────────────────────┐
        ▼                                            ▼
  设备端仓库                                    云平台端仓库
  xiaozhi-esp32/main/boards/deep-dog            deep-trace-com/{backend,frontend}
  实现：mqtt/ · ws-mcp/ · http-server/ · pairing/ 实现：backend/devices · frontend/src/iot
```

- **设备端**：以 [01-mqtt-contract.md](./01-mqtt-contract.md) / [02-ws-contract.md](./02-ws-contract.md) 为准实现固件；
  板级字段细节仍以 [mqtt/protocol/deep-dog-mqtt.yml](../mqtt/protocol/deep-dog-mqtt.yml) 为真源。
- **云平台端**：以 [01-mqtt-contract.md](./01-mqtt-contract.md) / [03-http-api-contract.md](./03-http-api-contract.md) 为准实现订阅/下发与页面。
  云端现有规格 [unified-mqtt-subscription.md](../../../../../deep-trace-com/docs/requirements/features/device-platform/unified-mqtt-subscription.md)
  与 UDM 规格的 uuid 骨架关系：**由本契约的统一骨架取代**（见 [01-mqtt-contract.md §2](./01-mqtt-contract.md#2-统一命名空间)）。

> 跨仓库路径提示：本仓库设备端 = `D:\working\test-platform\xiaozhi-esp32`；
> 云平台端 = `D:\working\test-platform\deep-trace-com`。

## 契约变更流程（双方约定）

1. **先改契约后改码**：任何跨端字段/主题变化，先提 PR 修改本目录文档 → 双端评审 → 再实现。
2. **版本号**：契约整体版本见 [01-mqtt-contract.md §7](./01-mqtt-contract.md)；单报文版本见信封 `schema` 字段。
3. **兼容性**：新增字段必须可选；改名/删除必须走弃用期（见 [01-mqtt-contract.md §7](./01-mqtt-contract.md)）。
4. **审计**：新增/修改接口需通过 [00-architecture-review.md §4.7](./00-architecture-review.md) 的接口审计清单。
5. **单一真源**：主题名与字段名不得在两端各自硬编码后"口头对齐"——必须沉淀到本目录 + `deep-dog-mqtt.yml`。

## 当前状态（基线快照）

- 契约版本：`interop-2`（统一命名空间**已落地**，2026-10）。
- **已定决议（D1–D6）**：见 [00-architecture-review.md 决策摘要](./00-architecture-review.md)。
- **实现进度**：
  - ✅ 设备端：topic 前缀可配置（`DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT` / NVS `topic_root`）；
    新增集中构造 `mqtt/mqtt_topics.h`；全模块 topic 迁移到统一骨架；`pairing/status` 等旧话题名已改。
  - ✅ 云端后端：前缀读 `MQTT_TOPIC_ROOT`；`ALL_CHANNELS` 移除 `pairing`（配对改 HTTP）；新增设备面解绑
    `POST /devices/pair/unbind`；下发的 `topics`/`mqtt_topic_prefix` 全为新骨架。
  - ✅ 前端：`mqttUnified.js`/`homeDevices.js` 统一 topic 构造（读 `VITE_MQTT_TOPIC_ROOT`）；
    14 个 composable/view 迁移；补齐 `dog`/`uart`/`arm` 模块面板（卡片与话题一一对应）。
  - ✅ 契约真源：`deep-dog-mqtt.yml` 重写为 `0.2.0`（44 topics，无重复键）。
- **端到端验证**：
  - 模拟设备（本地 EMQX + 后端 + 前端）：配对闭环（handshake→输码→bind→activate `bound=true`）；
    前端 IMU/CAN 页实时刷新、telemetry 落库（`TelemetrySample`）。
  - **真机（ESP32-S3，MAC `1051db8118fc`）**：`idf.py build/flash` 通过；设备日志
    `dog_mqtt: start prefix=deep-trace/1051db8118fc/`、`telemetry topic=…/status/telemetry`；
    订阅 `cmd/{pairing,stream,face,track,touch,handle,gimbal}`；云端 ingest 收到
    `status/face/registry` 并正确解析（`synced device_id=1051db8118fc`）。
- **保留项（弃用期内）**：旧前缀订阅双读（face/registry legacy）、`pairing/cmd` 下行双发、旧 `telemetry`
  DB 配置 topic 仍订阅。下线见 [00 §5 阶段四](./00-architecture-review.md)。
