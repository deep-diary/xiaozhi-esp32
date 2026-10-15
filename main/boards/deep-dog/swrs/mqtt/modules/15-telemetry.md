# 15 · telemetry（deep-trace 遥测通道）

| 项 | 内容 |
|----|------|
| module_id | `telemetry`（无入口卡；云端 ingest worker 消费） |
| capabilities | 不占 `capabilities.*`；由云端工位配置决定是否启用 |
| 路由建议 | 无设备详情页；数据进 deep-trace `TelemetrySample` |
| 契约 | ready（字段已定） |
| 对齐 | deep-trace `docs/interface/10-mqtt-telemetry-ingest.md`；本仓 YAML `topics.telemetry` |

## 目标

把设备侧可数值化的运行时指标，按**云端为工位分配的确切话题**上报，供 deep-trace 入库/时序展示。

## ⚠️ 话题前缀（最容易搞混）

遥测 **不** 挂模块通道前缀 `deepdiary/deep-dog/{device_id}/`。它由云端 seed 按工位分配，设备 **不得自选**：

```text
deep-trace/{home_code}/{work_unit_code}/telemetry
例：deep-trace/HOME-DEMO/SPARKBOT/telemetry
```

- 话题真值来自：① HTTP 配对握手返回的 `topics`（见 [16-http-pairing](./16-http-pairing.md)）→ ② NVS `deep_dog_mqtt.telemetry_topic` 覆盖。
- 两者皆空 → 遥测关闭，设备不发。
- QoS=0，retain=false。

## 字段表 · `telemetry`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `home_code` | string | 是 | 与话题一致；未知归属整条丢弃 |
| `work_unit_code` | string | 是 | 与话题一致 |
| `device_program` | string | 是 | 设备程序名，如 `esp_sparkbot`（NVS `device_program`，默认编译宏） |
| `ts` | string | 是 | ISO8601 带时区（取 SNTP 时钟；未同步时用 boot 兜底） |
| `points` | object | 是 | key=`field_code`；value 为 number 或 `{value,unit}`，务必可转数值 |

## 上报 points（设备侧默认集）

| field_code | 来源 | 单位 |
|------------|------|------|
| `free_heap` | `esp_get_free_heap_size()` | byte |
| `min_free_heap` | `esp_get_minimum_free_heap_size()` | byte |
| `internal_free` | internal heap free | byte |
| `psram_free` | PSRAM heap free（无则省略） | byte |
| `rssi` | STA RSSI | dBm |
| `uptime_s` | 运行秒数 | s |

## 上报节奏

5–60s；本板默认复用 `device/status` 心跳节奏（约 5s）另加节流（默认 15s，可 `DEEP_DOG_TELEMETRY_INTERVAL_US` 覆盖）。

## 固件实现

| 项 | 落地 |
|----|------|
| 模块 | [`mqtt/modules/telemetry_mqtt.cc`](../../../mqtt/modules/telemetry_mqtt.cc) |
| 配置 | NVS `deep_dog_mqtt`：`telemetry_topic` / `home_code` / `work_unit_code` / `device_program` |
| 配置注入 | HTTP 配对 200 响应的 `topics.telemetry` 与 `home_code`；或配网页/NVS 手填（联调） |
| 兜底 | `home_code`/`work_unit_code` 缺省时从 `telemetry_topic`（`deep-trace/{home}/{wu}/telemetry`）反推 |

## 验收

- [x] 配置了 topic 后，设备发布到 `deep-trace/{home}/{wu}/telemetry`（非模块前缀）
- [x] 云端 `TelemetrySample` 能按 `(工位, field_code, ts)` 查到
- [x] 未配置 topic 时不发布，无报错

## 非目标

- 不负责云端种子/工位配置下发（seed 属 deep-trace）。
- 不上报人脸大图 / 密钥。
