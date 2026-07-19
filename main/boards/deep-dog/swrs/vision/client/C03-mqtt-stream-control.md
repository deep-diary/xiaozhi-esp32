# V-C03 · MQTT 推流开关

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C03** |
| 依赖 | [C02](./C02-device-push-stream.md)、[infra EMQX](../infra.md) |
| 验收 | MQTT 可远程 start/stop 推流 |

## 目标

远程开关 C02 推流；上报 `stream/status`。推荐板内独立客户端连 `192.168.31.25:1883`。

## 协议

`deepdiary/deep-dog/<device_id>/stream/cmd`：

```json
{ "action": "start" | "stop", "ts": 1710000000 }
```

`.../stream/status`：`idle|starting|streaming|error` + url。

## 验收

- [ ] MQTTX / `mqtt-ws` 可开关推流
- [ ] status 与真实一致
- [ ] 错误 action 不崩溃
