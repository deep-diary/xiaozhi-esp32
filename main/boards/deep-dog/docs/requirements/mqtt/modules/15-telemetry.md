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

## ⚠️ 话题前缀（已并入统一骨架）

原由云端 seed 按工位分配的话题 `deep-trace/{home_code}/{work_unit_code}/telemetry` **已迁移**到
统一骨架（见 [01-mqtt-contract §5.5](../../../interface/01-mqtt-contract.md)）：

```text
{prefix}/{device_id}/status/telemetry
例：deep-trace/1051db8118fc/status/telemetry
```

- **旧式话题兼容**：NVS `deep_dog_mqtt.telemetry_topic` 仍可显式指定**非旧式**话题覆盖（联调）；
  若为旧式 `deep-trace/{home}/{wu}/telemetry` 形状，则**改用统一骨架**（避免双轨）。
- `home_code` / `work_unit_code` 缺省时从 `telemetry_topic` 反推（兜底）。
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

## 数据点自动归并（Data Point Registry · interop 04）

自 Phase 7 起，`points` 由**数据点注册表**自动汇入：凡在
[04-datapoint-registry.md](../../../interface/04-datapoint-registry.md) 中声明了
`DP_CH_TELEMETRY` 通道的**数值点**（int / float），由发布引擎自动出现在 `points` 中，
**无需在此手工维护字段表**。

- **格式不变**：仍为 object，`key = field_code`，`value = number` 或 `{value, unit}`（兼容云端 ingest）。
- **key 规则**：默认取点 id 的**最后一段**（如 `device.free_heap` → `free_heap`）；子实例为 `<sub>_<point>`。
- **上表 6 个默认点由数据点提供**（key 名保持不变，平滑替换原手写实现）：

| field_code | 数据点 |
|------------|--------|
| `free_heap` | `device.free_heap` |
| `min_free_heap` | `system.min_free_heap` |
| `internal_free` | `system.internal_free` |
| `psram_free` | `system.psram_free` |
| `rssi` | `device.rssi` |
| `uptime_s` | `device.uptime_s` |

- **未标记** `DP_CH_TELEMETRY` 的点**不进入** telemetry（避免高频 / 低价值点污染时序库）。
- 为某点加上 `DP_CH_TELEMETRY` 即自动出现在 telemetry（如 `imu.*` 姿态数值点）。
- **单位**：`value` 的 `unit` 取自数据点声明的规范单位（如 `bytes`）；与旧手写的 `B` 字符串可能不同，
  但**值语义不变**（云端按 `field_code` + `ts` 入库，`unit` 仅作展示）。
- **改动点 id 时须同步核对**：`field_code` 默认取点 id 最后一段，**重命名数据点会改变上报字段名** →
  必须同步本表与云端（避免云端收不到旧字段）。

## 上报节奏

5–60s；本板默认复用 `device/status` 心跳节奏（约 5s）另加节流（默认 15s，可 `DEEP_DOG_TELEMETRY_INTERVAL_US` 覆盖）。

## 固件实现

| 项 | 落地 |
|----|------|
| 模块 | [`mqtt/modules/telemetry_mqtt.cc`](../../../../mqtt/modules/telemetry_mqtt.cc) |
| 配置 | NVS `deep_dog_mqtt`：`telemetry_topic` / `home_code` / `work_unit_code` / `device_program` |
| 配置注入 | HTTP 配对 200 响应的 `topics.telemetry` 与 `home_code`；或配网页/NVS 手填（联调） |
| 兜底 | `home_code`/`work_unit_code` 缺省时从 `telemetry_topic`（`deep-trace/{home}/{wu}/telemetry`）反推 |

## 验收

- [x] 配置了 topic 后，设备发布到 `{prefix}/{device_id}/status/telemetry`（统一骨架）
- [x] 云端 `TelemetrySample` 能按 `(工位, field_code, ts)` 查到
- [x] 未配置 topic 时不发布，无报错
- [ ] `points` 由数据点注册表**自动汇入**（Phase 7）：6 个默认点 `field_code` 与既有保持一致（平滑替换手写实现）
- [ ] 未标记 `DP_CH_TELEMETRY` 的数据点**不出现**在 `points`

## 非目标

- 不负责云端种子/工位配置下发（seed 属 deep-trace）。
- 不上报人脸大图 / 密钥。
