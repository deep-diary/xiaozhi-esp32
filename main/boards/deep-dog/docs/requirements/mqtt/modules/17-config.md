# 17 · config（统一配置中心通知通道）

| 项 | 内容 |
|----|------|
| module_id | `config`（无入口卡；配置管理走 deep-trace 后端） |
| capabilities | 不占 `capabilities.*`；由编译开关 `DEEP_DOG_MQTT_ENABLE` 决定 |
| 路由建议 | 无设备详情页；配置在 deep-trace 后端维护 |
| 契约 | ready（字段已定） |
| 需求 | [CFG-01 统一配置中心](../../config/CFG-01-unified-config-center.md) |
| 固件 | [`config_center/`](../../../../config_center/) · [`mqtt/modules/config_mqtt.cc`](../../../../mqtt/modules/config_mqtt.cc) |

## 目标

作为**配置中心的下行通知通道**：服务器配置变更后，通过 MQTT 秒级通知设备，
设备收到即触发一次 HTTP `config/pull` 拉取最新配置。HTTP 定时轮询为兜底（默认 5 分钟）。

配置**内容本身不走 MQTT**，全部通过 HTTP 下发（见 CFG-01 §4）。

## 话题

| 话题 | 方向 | QoS | retain | 说明 |
|------|------|-----|--------|------|
| `{prefix}/{device_id}/cmd/config` | 下行 | 1 | false | 配置变更通知 |
| `{prefix}/{device_id}/cmd/config/ack` | 上行 | 0 | false | 收到通知后的即时回执 |

## cmd/config 载荷

```json
{ "action": "refresh", "config_version": 5, "reason": "template_updated" }
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | 是 | 目前仅 `refresh` |
| `config_version` | int | 否 | 服务器最新版本（供参考，设备以 pull 结果为准） |
| `reason` | string | 否 | `template_updated` / `manual_update` / `bulk_push` |
| `ts` | int | 否 | Unix 秒 |

## cmd/config/ack 载荷（信封）

统一信封：`schema=deep-trace.cmd.v1.ack`，业务字段在 `data`：

| 字段 | 类型 | 说明 |
|------|------|------|
| `action` | string | 原样回显 |
| `result` | string | `accepted` / `unsupported` |
| `config_version` | int | 设备当前生效版本 |
| `has_remote_config` | bool | 是否已成功从服务器同步过 |

> **注意**：`ack` 仅表示"已受理并安排拉取"，**不代表配置已生效**。
> 生效确认由 HTTP `POST /devices/config/ack` 完成（见 CFG-01 §4.4）。

## 触发链路

```
服务器配置变更
  → 后端 publish_config_refresh() 发 cmd/config
  → 设备 config_mqtt.OnMessage → ConfigManager::RequestSync()
  → 后台任务立即执行 HTTP config/pull
  → 解析 + 差异分发（ConfigChangeHub）→ 模块热更新
  → HTTP config/ack 上报结果
```

## 兜底

- MQTT 断连 / 通知丢失：HTTP 定时轮询（默认 300s）保证最终一致。
- MQTT 通道未启用：仅 HTTP 轮询可用。

## 验收

- [ ] 服务器发 `cmd/config` 后，设备 5s 内完成一次 pull
- [ ] 设备回 `cmd/config/ack`（result=accepted）
- [ ] MQTT 断连时，HTTP 轮询仍能同步配置

## 非目标

- 不在 MQTT 上传输完整配置（避免报文过大；统一走 HTTP）。
- 不负责配置模板管理（属 deep-trace 后端 / 前端）。
