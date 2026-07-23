# 02 · stream（流媒体）

| 项 | 内容 |
|----|------|
| module_id | `stream` |
| capabilities | `stream` |
| 路由建议 | `/device/:deviceId/modules/stream` |
| 契约 | ready |
| YAML | `stream/cmd`、`stream/status` |
| 路线图 | **V-C03**（原 M02） |
| 依赖 | [C02 推流](../../vision/client/C02-device-push-stream.md)、[infra](../../vision/infra.md) |

## 入口卡文案

- 标题：流媒体  
- 说明：远程开关 RTSP 推流  

## 详情页目标

显示 `state` / `mode` / `url` / `error`；提供 start / stop 控制。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `stream/status` | ↑ | 0 | true |
| `stream/cmd` | ↓ | 1 | false |

## 样例 JSON

**status**

```json
{
  "state": "idle",
  "mode": "off",
  "url": "https://live.deep-diary.com/deep-dog/dev/index.m3u8",
  "error": "",
  "ts": 1710000000,
  "ts_iso": "2024-03-09T16:00:00Z"
}
```

**cmd**

```json
{ "action": "start", "mode": "rtsp_push", "ts": 1710000000 }
```

- `action` 必填；`mode` 可选（缺省 `start`→`rtsp_push`，`stop`→`off`）。
- `url`：网页可播的外网 HLS（设备仍推局域网 RTSP，见 [infra](../../vision/infra.md)）。
- `ts`：unix 秒；`ts_iso`：UTC 可读时间（前端优先展示 `ts_iso`）。
- 字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 若 `capabilities.stream !== true`：不渲染入口卡；直链本页则提示不可用。
- **Step 2** mount：订阅 `stream/status`。
- **Step 3** 渲染 state/mode/url/error。
- **Step 4** 按钮发布 `stream/cmd`：`start` / `stop`。
- **Step 5** unmount：取消订阅。
- **Step 6** 显示离线/超时态。

## 固件实现

映射 HTTP：

- `start` → `POST /api/vision_publish?mode=rtsp_push`
- `stop` → `mode=off`

对齐 `VisionPushStatus` / `GET /api/status` 的 `push_status`、`push_url`、`mode`。状态变更即时发 `stream/status`（retain）。非法 action 不崩溃，可带 `error`。

同客户端宜一并发布 [04-face](./04-face.md)。

### 固件验收

- [ ] MQTTX 可开关推流
- [ ] status 与真实 push 一致
- [ ] 错误 action 不崩溃

## 验收（前端）

- [ ] 详情页可看状态、可发 start/stop
- [ ] 无 capability 时入口卡隐藏
