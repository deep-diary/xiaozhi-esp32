# 16 · HTTP 配对（对接 deep-trace 服务器）

| 项 | 内容 |
|----|------|
| module_id | `pairing`（HTTP 路径；与 [00-pairing](./00-pairing.md) MQTT 路径并存） |
| capabilities | 核心；显式触发配对流 |
| 路由建议 | 无独立入口卡；网页「添加设备」输码消费 |
| 契约 | ready（字段已定） |
| 对齐 | deep-trace `docs/requirements/features/iot/144-device-http-pair-api.md`；云端绑定入口 143 |

## 目标

除既有 MQTT 配对（[00-pairing](./00-pairing.md)）外，新增对接 **deep-trace 服务器** 的 HTTP 配对：服务器生成 6 位码，设备握手拿码→屏显+语音→轮询确认→切身份连业务 MQTT。

两条路径 **幂等共存**：谁先绑定谁生效，重复无副作用；OTA/官方激活链路不动。

## 触发与选择

| 条件 | 路径 |
|------|------|
| NVS `deep_dog_mqtt.server_api_base` 非空 | 走 **HTTP 配对**（本节） |
| `server_api_base` 为空 | 走既有 MQTT 配对（[00-pairing](./00-pairing.md)） |

服务器基址来自配网页「deep-trace 服务器 API 地址」（见 [N03](../../net/N03-wifi-portal-mqtt-broker.md)），NVS `deep_dog_mqtt.server_api_base`，形如 `https://<后端>/api/v1`。

## 流程

```text
触发（MCP start_pairing / hold1_tap2 / cmd/pairing start_pairing）
  → POST {base}/devices/pair/handshake   Header: Device-Id: <MAC紧凑>
     ├─ 200 {bound:false, activation:{code,message,challenge,timeout_ms}, mqtt:{host,port,path,tls}}
     │     → 屏显 + 语音播报 activation.code → 进入轮询
     ├─ 200 {bound:true, device_id, mqtt_topic_prefix, mqtt, topics}
     │     → 直接应用身份 + 连业务 MQTT
     └─ 429 握手过频（30s 限流）→ 退避重试
  → 轮询 POST {base}/devices/pair/activate  body {device_id, challenge?, hmac?}
     ├─ 200 {device_id, mqtt_topic_prefix, mqtt, topics} → 绑定成功
     ├─ 202 {bound:false, expires_in} → 继续轮询（3–5s 退避）
     ├─ 404 / 410 → 终止态，立即停止，回 idle，可重新握手
     ├─ 401 → 鉴权失败（HMAC），终止
     └─ 429 / 网络错误 → 上限重试后终止
```

## 字段表 · handshake 请求

| 项 | 值 |
|----|----|
| URL | `{base}/devices/pair/handshake` |
| Method | POST |
| Header | `Device-Id: <MAC 紧凑>`（或 body `device_id`） |
| Body | `{ "mac": "AA:BB:…", "fw": "<固件版本>" }` |

## 字段表 · activate 请求

| 项 | 类型 | 必填 | 说明 |
|----|------|------|------|
| `device_id` | string | 是 | MAC 紧凑串 |
| `challenge` | string | 否 | 握手返回的 challenge |
| `hmac` | string | 否 | `HMAC-sha256(challenge)`（key 在 efuse）；联调期服务端可能未启用 |

## 身份/配置落盘（NVS `deep_dog_mqtt`）

| key | 来源 | 说明 |
|-----|------|------|
| `bound` | 200 | `true` |
| `device_id` | 200 `device_id` | 绑定身份（MAC 紧凑） |
| `broker_host` / `broker_port` / `broker_tls` | 200 `mqtt` | 业务 Broker（覆盖门户/默认） |
| `telemetry_topic` / `home_code` / `work_unit_code` | 200 `topics` | 遥测通道，见 [15-telemetry](./15-telemetry.md) |
| `server_api_base` | 门户 | 保留 |

切换身份 **不可在 HTTP/MQTT 回调线程内 Stop/Start 客户端**；defer 到主循环重载（同 00-pairing）。

## 健壮性

- 独立任务 / 状态机，**不阻塞主循环**。
- 全链路硬超时 = `handshake.activation.timeout_ms`（兜底 120s）。
- 支持 **取消**（触摸 / MCP）。
- 轮询间隔 3–5s + 指数退避；网络错误有上限重试。

## 固件实现

| 项 | 落地 |
|----|------|
| 模块 | [`pairing/deep_dog_pair_http.cc`](../../../../pairing/deep_dog_pair_http.cc) |
| 触发 | `DeepDogMqtt::StartPairingSessionOrAnnounceBound` → 有 `server_api_base` 走 HTTP |
| 应用身份 | 复用 `ReloadDeviceIdentity()`（defer 主循环） |
| HTTP | `esp_http_client` + cert bundle（HTTPS） |
| Broker 防护 | 200 响应的 `mqtt.host` 为 **回环/未指定**（`127.0.0.1`/`localhost`/`0.0.0.0`）时**不覆盖**本地 broker（仅保留遥测/工位配置） |
| 服务器基址 | NVS `server_api_base`；编译默认 `DEEP_DOG_MQTT_DEFAULT_SERVER_API_BASE`（默认空） |

## 验收

- [x] 握手拿到码 → 网页绑定 → 轮询 200 拿到 mqtt/topics → 切身份连业务 MQTT
- [x] 轮询在 404/410 时 **停止**；网络错误有上限重试（代码路径；实机 404/410 待覆盖）
- [x] 429 握手限流有退避（代码路径）
- [x] 配对等待不阻塞主循环；可取消（MCP `self.device.cancel_pairing`）
- [x] 与 MQTT 配对共存：重复无副作用

## 非目标

- 官方 xiaozhi.me / OTA 激活兼容（不动）。
- 完整 HMAC 强校验（联调期先放行，代码结构预留）。
