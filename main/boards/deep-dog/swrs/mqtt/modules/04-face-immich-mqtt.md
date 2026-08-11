# 04-face · Immich MQTT 配置与状态（HTTP 关闭剖面）

| 项 | 内容 |
|----|------|
| 依赖 | [S05 Immich 真名](../../vision/server/S05-immich-real-name.md) · [04-face](./04-face.md) |
| 背景 | 联调剖面 `DEEP_DOG_HTTP_SERVER_ENABLE=0`，8080 为 WS MCP；Immich 配置/状态改走 MQTT |
| 代码 | `face_ai/immich_client.*` · `mqtt/modules/face_mqtt.cc` |

## Topic

| Topic | 方向 | retain | 说明 |
|-------|------|--------|------|
| `…/face/immich/status` | ↑ | **true** | 配置摘要 + 服务器探活 + 上次 upload/poll 结果 |

## `face/cmd` 扩展 action

| action | 字段 | 说明 |
|--------|------|------|
| `set_immich_config` | `api_url?`, `api_key?`, `delete_asset?` | 写入 NVS `fdog_im`；`api_key` 省略则保留原 Key |
| `ping_immich` | — | 立即 `GET {api_url}/server/ping`，刷新 `face/immich/status` |

也可在**无 action** 时仅带 `api_url` / `api_key` / `delete_asset`（稀疏更新，与 HTTP POST 等价）。

## `face/immich/status` 载荷

```json
{
  "configured": true,
  "url": "http://192.168.31.25:2283/api",
  "key_len": 32,
  "delete_asset": 0,
  "server_ok": true,
  "server_http": 200,
  "ping_ms": 42,
  "inflight": 0,
  "last": "idle",
  "last_local_id": 0,
  "ts": 1710000000
}
```

- **禁止**下发 Key 明文；仅 `key_len`。
- `server_ok`：`configured && ping 200`。
- `last`：`idle` / `no_key` / `upload_ok` / `upload_fail` / `unknown` / `has_name` 等。

## 默认配置

| 项 | 默认 |
|----|------|
| `api_url` | `http://192.168.31.25:2283/api`（`face_ai_config.h`） |
| `api_key` | 空；开发者可复制 `face_ai_secrets.h.example` → `face_ai_secrets.h` 编译注入，或经 MQTT/前端下发 |

## 验收

- [ ] 前端 Face 页显示 Immich 服务器状态（绿/红）
- [ ] MQTT `set_immich_config` 后 retain 更新且 `key_len>0`
- [ ] `ping_immich` 后 `server_ok` 与局域网 Immich 一致
- [ ] 配置 Key 后识别出镜可 upload（`last=upload_ok` 或 `name_pending`）
