# 02 · stream（流媒体）

| 项 | 内容 |
|----|------|
| module_id | `stream` |
| capabilities | `stream` |
| 路由建议 | `/device/:deviceId/modules/stream` |
| 契约 | ready |
| YAML | `stream/cmd`、`stream/status`、`stream/photo` |
| 路线图 | **V-C03**（原 M02） |
| 依赖 | [C02 推流](../../vision/client/C02-device-push-stream.md)、[infra](../../vision/infra.md) |

## 入口卡文案

- 标题：流媒体  
- 说明：推流与人脸框叠加  

## 详情页目标

1. 显示 `state` / `mode` / `url`（外网 HLS）/ `lan_url` / `push_url` / `error`；start / stop；嵌 HLS 或外链 `url`。  
2. **人脸叠加（推荐）**：若 `capabilities.face`，同页订 `face/status`；UI 开关「显示人脸框 / 显示人名」。  
3. **跟踪（推荐）**：若 `capabilities.track`，同页订 `track/status`，开关发 `track/cmd`；可 `#track` 锚点。  
4. **拍照视觉解释（推荐）**：按钮发 `stream/cmd` `take_photo`，订 `stream/photo` 展示结果（与推流并存，不改 mode）。  
5. 人脸总开关 / 间隔 / 清库可链到 [04-face](./04-face.md)，或本页简化发 `face/cmd`。

## 边界

| 归属 | Topic / API | 内容 |
|------|-------------|------|
| 远程开关 + 推流态 | `stream/cmd`、`stream/status` | 本模块 |
| 局域网 MJPEG 预览 | HTTP `/stream` + `/api/status` | `mode=stream` 时本机页用；网页优先 `url` HLS |
| 人脸框 / 人名 | [`face/status`](./04-face.md) | 推流时可并存；坐标像素相对 `w`×`h` |
| 跟踪目标 | [`track/status`](./05-track.md) | UI 宿主为本页，非 Face 页 |
| Immich 轮播 / 打招呼 | [`04-face`](./04-face.md) | 不在本页做 Kiosk |

设备推 **局域网 RTSP**；网页播 **外网 HLS**（`url`）。勿把 `push_url` 当浏览器播放地址。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `stream/status` | ↑ | 0 | true |
| `stream/cmd` | ↓ | 1 | false |
| `stream/photo` | ↑ | 0 | false |

前缀：`deepdiary/deep-dog/{device_id}/`。字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 字段表 · `stream/cmd`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | enum | 是 | `start` / `stop` / `take_photo` |
| `mode` | enum | 否 | `off` / `stream` / `rtsp_push`；`start` 缺省 → `rtsp_push`；`stop` 恒强制 `off`；**`take_photo` 忽略** |
| `question` | string | 否 | 仅 `take_photo`；默认「描述画面里有什么」 |
| `ts` | int | 否 | Unix 秒 |

### 样例 · 拍照视觉解释

```json
{ "action": "take_photo", "question": "画面里有什么？简单描述。", "ts": 1710000000 }
```

异步执行，约数秒；结果走 `stream/photo`（不改推流 `mode`）。并发第二次会 `ok=false, error=busy`。需设备已配置 Explain URL（曾连小智云下发 vision）。

## 字段表 · `stream/photo`

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | bool | 是否成功 |
| `result` | string | 成功时 Explain 返回（多为 **字符串化 JSON**，内含 `text`） |
| `error` | string | `busy` / `capture_fail` / `Image explain URL or token is not set` 等 |
| `elapsed_ms` | int | 耗时 |
| `ts` | int | Unix 秒 |

## 字段表 · `stream/status`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `state` | enum | 是 | `idle` / `starting` / `streaming` / `error` |
| `mode` | enum | 是 | `off` / `stream`（HTTP MJPEG）/ `rtsp_push` |
| `url` | string | 是 | 外网 HLS 播放地址（编译期配置；即使 `mode=off` 也常带完整 URL，便于 UI 预填） |
| `lan_url` | string | 否 | 局域网 HLS |
| `push_url` | string | 否 | 设备 RTSP 发布地址；未推流时可为空 |
| `error` | string | 否 | 空字符串表示无错；见下表 |
| `ts` | int | 是 | Unix 秒 |
| `ts_iso` | string | 否 | UTC ISO8601；前端展示优先用此字段 |

### `state` 语义

| state | 含义 | 典型 UI |
|-------|------|---------|
| `idle` | 未推流 / 已停止 | 可点「开始」 |
| `starting` | 正在连 MediaMTX / 尚无 RTP | 按钮 loading；可显示 `error=no_rtp` |
| `streaming` | 推流正常 | 播放器可播；可点「停止」 |
| `error` | 推流失败 | 展示 `error`；可重试 start |

`mode=rtsp_push` 且底层仍 Idle 时，固件对外会报 `starting`（更贴切）。

### `error` 代码

| code | 来源 | 说明 |
|------|------|------|
| `""` | — | 正常 |
| `invalid_json` | cmd | 报文无法解析 |
| `missing_action` | cmd | 缺 `action` |
| `bad_mode` | cmd | `mode` 非法 |
| `start_requires_mode` | cmd | `start` 却解析成 `off` |
| `unknown_action` | cmd | 非 start/stop |
| `stream_unavailable` | 固件 | Hub/HTTP 未就绪 |
| `push_error` | 推流 | `state=error` 且无更细错误 |
| `no_rtp` | 推流 | `starting` 且尚未收到 RTP |

## 样例 JSON · `stream/cmd`

**开始推流（推荐缺省）**

```json
{ "action": "start", "ts": 1710000000 }
```

**显式 RTSP 推流**

```json
{ "action": "start", "mode": "rtsp_push", "ts": 1710000000 }
```

**局域网 MJPEG（调试）**

```json
{ "action": "start", "mode": "stream", "ts": 1710000000 }
```

**停止**

```json
{ "action": "stop", "ts": 1710000000 }
```

## 样例 JSON · `stream/status`

**空闲**

```json
{
  "state": "idle",
  "mode": "off",
  "url": "https://live.deep-diary.com/deep-dog/dev/index.m3u8",
  "lan_url": "http://192.168.31.25:8888/deep-dog/dev/index.m3u8",
  "push_url": "",
  "error": "",
  "ts": 1710000000,
  "ts_iso": "2024-03-09T16:00:00Z"
}
```

**启动中（握手 / 等 RTP）**

```json
{
  "state": "starting",
  "mode": "rtsp_push",
  "url": "https://live.deep-diary.com/deep-dog/dev/index.m3u8",
  "lan_url": "http://192.168.31.25:8888/deep-dog/dev/index.m3u8",
  "push_url": "rtsp://192.168.31.25:8554/deep-dog/dev",
  "error": "no_rtp",
  "ts": 1710000005,
  "ts_iso": "2024-03-09T16:00:05Z"
}
```

**推流中**

```json
{
  "state": "streaming",
  "mode": "rtsp_push",
  "url": "https://live.deep-diary.com/deep-dog/dev/index.m3u8",
  "lan_url": "http://192.168.31.25:8888/deep-dog/dev/index.m3u8",
  "push_url": "rtsp://192.168.31.25:8554/deep-dog/dev",
  "error": "",
  "ts": 1710000010,
  "ts_iso": "2024-03-09T16:00:10Z"
}
```

**失败**

```json
{
  "state": "error",
  "mode": "rtsp_push",
  "url": "https://live.deep-diary.com/deep-dog/dev/index.m3u8",
  "lan_url": "http://192.168.31.25:8888/deep-dog/dev/index.m3u8",
  "push_url": "rtsp://192.168.31.25:8554/deep-dog/dev",
  "error": "push_error",
  "ts": 1710000020,
  "ts_iso": "2024-03-09T16:00:20Z"
}
```

**非法 cmd 反馈（仍 retain 最新 status）**

```json
{
  "state": "idle",
  "mode": "off",
  "url": "https://live.deep-diary.com/deep-dog/dev/index.m3u8",
  "lan_url": "http://192.168.31.25:8888/deep-dog/dev/index.m3u8",
  "push_url": "",
  "error": "unknown_action",
  "ts": 1710000030,
  "ts_iso": "2024-03-09T16:00:30Z"
}
```

## Steps（前端）

- **Step 1** 若 `capabilities.stream !== true`：不渲染入口卡；直链本页则提示不可用。
- **Step 2** mount：订阅 `stream/status`（retain）；若有 `face` → 订 `face/status`；若有 `track` → 订 `track/status`。
- **Step 3** 渲染 `state`/`mode`/`error`；播放器用 **`url`（外网 HLS）**；可附链 `lan_url`；`push_url` 仅调试。
- **Step 4** 叠加层开关：显示人脸框 / 人名 / 跟踪十字（用 `faces[]` 与 `track/status.target`，像素/`w`/`h` 归一化到播放器）。
- **Step 5** `state===idle|error` 可发 start；`streaming|starting` 可发 stop；`starting` loading。
- **Step 6** 发布 `stream/cmd`；跟踪用 `track/cmd`；人脸总开关可链 Face 页或本页发 `face/cmd`。
- **Step 7** 时间优先 `ts_iso`；`error` 非空 toast。
- **Step 8** unmount：退订本页所订 Topic。

## 固件实现

映射 HTTP：

- `start` → `POST /api/vision_publish?mode=rtsp_push`（或 cmd 指定 `stream`）
- `stop` → `mode=off`

对齐 `VisionPushStatus` / `GET /api/status`。推流时可同时做人脸（`DURING_RTSP=1`）。非法 action 不崩溃，写入 `error`。

### 固件验收

- [ ] MQTTX 可开关推流
- [ ] status 与真实 push 一致（含 `lan_url`/`push_url`）
- [ ] 推流中 `face/status` 可有框（人脸已开时）
- [ ] 错误 action 不崩溃，`error` 有码

## 验收（前端）

- [ ] 详情页可看状态、可发 start/stop
- [ ] 可用 `url` 打开/嵌入 HLS
- [ ] 可选叠人脸框 / 跟踪 / 人名
- [ ] `starting` / `error` / `no_rtp` 有明确反馈
- [ ] 无 capability 时入口卡隐藏
