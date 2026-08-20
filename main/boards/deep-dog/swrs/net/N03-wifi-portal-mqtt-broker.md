# N03 · 配网页配置板级 MQTT Broker

| 项 | 内容 |
|----|------|
| ID | N03 |
| 依赖 | WiFi 配网 AP（`78/esp-wifi-connect`）· [M01](../mqtt/M01-board-mqtt-protocol.md) |
| 代码 | vendored `components/esp-wifi-connect` · [`mqtt/mqtt_config.cc`](../../mqtt/mqtt_config.cc) · [`esp_sparkbot_board.cc`](../../esp_sparkbot_board.cc) |
| 关联 | [N02 STA](./N02-sta-address.md) · [mqtt/README](../mqtt/README.md) |

## 目标

在配网热点 `Xiaozhi-xxxx` → `http://192.168.4.1` → **高级选项**中，可修改 **板级 MQTT**（deep-dog 业务 broker，非小智云 `mqtt.xiaozhi.me`）的 host/port，**无需重编译固件**。

同时恢复高级页 **自定义 OTA URL** 可见性（组件默认 `show_ota_config=false`，非功能删除）。

## 行为

| 项 | 约定 |
|----|------|
| 入口 | 配网 Web UI「高级」Tab |
| 字段 | `broker_host`（必填语义）、`broker_port`（默认 1883） |
| 持久化 | NVS 命名空间 `deep_dog_mqtt`：`broker_host` / `broker_port`（与 `DeepDogMqttConfig::Save` 一致） |
| 生效 | 保存后按组件既有 advanced 流程（重启/提示）；下次 `DeepDogMqtt::Start` / 开机 `Load()` 读新地址 |
| 编译默认 | 仍由 `DEEP_DOG_MQTT_DEFAULT_BROKER_*` 提供；NVS 非空则覆盖 |
| 范围外 | username/password 本轮不进门户；小智云 MQTT 不改 |

## 配网等待

进入配网 AP 后 **不得**再死等 STA IP（约 30s）。见板级 `StartNetwork`：`IsConfigMode` / `kDeviceStateWifiConfiguring` 时跳过等待。

## Broker 自动迁移

仅迁移明确废弃的旧联调地址（如 `192.168.31.25`）。**不得**把用户手填的 `broker.emqx.io` 强迁到编译默认，以免冲掉门户配置。

## 验收

- [ ] 高级页可见：Custom OTA URL + MQTT Broker/Port
- [ ] 保存后 NVS `deep_dog_mqtt` 含新 host/port；重启串口 `dog_mqtt_cfg: broker …`
- [ ] 配网模式不再卡约 30s（跳过 STA IP 等待）
- [ ] 手填 `broker.emqx.io` 重启后不被 migrate 改掉
