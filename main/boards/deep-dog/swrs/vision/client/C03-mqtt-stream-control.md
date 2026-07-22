# V-C03 · MQTT 推流开关

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C03** |
| 依赖 | [C02](./C02-device-push-stream.md)、[infra EMQX](../infra.md)、[M01 板级 MQTT](../../mqtt/M01-board-mqtt-protocol.md) |
| 协议真源 | [`../../mqtt/protocol/deep-dog-mqtt.yml`](../../mqtt/protocol/deep-dog-mqtt.yml) |
| 验收 | MQTT 可远程 start/stop 推流；`stream/status` / `face/status` 可订阅且与真实一致 |

## 目标

远程开关 C02 推流；上报完整 `stream/status`；同客户端可上报 `face/status`（有人、主脸中心坐标，供后续跟脸）。推荐板内独立客户端连 `192.168.31.25:1883`。

映射同一 HTTP 状态机：`start` → `POST /api/vision_publish?mode=rtsp_push`；`stop` → `mode=off`。

## 协议（摘要）

完整字段见 YAML。前缀：`deepdiary/deep-dog/<device_id>/`。

### `stream/cmd`（↓ QoS=1）

```json
{ "action": "start" | "stop", "mode": "off" | "stream" | "rtsp_push", "ts": 1710000000 }
```

- `action` 必填；`mode` 可选（缺省时 `start`→`rtsp_push`，`stop`→`off`）。
- 非法 `action`：不崩溃；`stream/status.state` 可为 `error` 并带 `error` 文案。

### `stream/status`（↑ QoS=0，retain）

```json
{
  "state": "idle" | "starting" | "streaming" | "error",
  "mode": "off" | "stream" | "rtsp_push",
  "url": "rtsp://192.168.31.25:8554/deep-dog/dev",
  "error": "",
  "ts": 1710000000
}
```

对齐固件 `VisionPushStatus` / `GET /api/status` 的 `push_status`、`push_url`、`mode`。状态变更即时发布。

### `face/status`（↑，关联；真源见 M01 / YAML）

同客户端宜发布人脸态，便于网页与后续跟踪：

```json
{
  "enabled": true,
  "has_person": true,
  "n": 1,
  "w": 640,
  "h": 480,
  "primary": { "cx": 320, "cy": 240, "score": 0.9 },
  "faces": [
    {
      "x0": 100, "y0": 80, "x1": 220, "y1": 240,
      "cx": 160, "cy": 160, "score": 0.9,
      "local_id": 2, "display_name": "#2"
    }
  ],
  "ts": 1710000000
}
```

坐标为像素，相对 `w`×`h`（与 `GET /api/face` 一致）。`has_person` 同 HTTP `has_face`。

### `face/cmd`（↓，可选本切片）

```json
{ "enabled": true, "ts": 1710000000 }
```

映射 `POST /api/face_enable?enabled=0|1`。

## 验收

- [ ] MQTTX / `mqtt-ws` 可开关推流
- [ ] `stream/status` 与真实 `push_status` / URL / mode 一致
- [ ] 可订阅 `face/status`，有人时 `has_person` 与 `primary.cx/cy` 合理
- [ ] 错误 action 不崩溃
