# V-S02 · HTTP Server：局域网 MJPEG

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S02** |
| 优先级 | P0 |
| 依赖 | [S01](./S01-http-dog-motion.md) |
| 下一切片 | [S03 人脸框](./S03-http-face-overlay.md) |
| 代码落点 | `http-server/`（`EspVideo` + `image_to_jpeg`） |
| 基线状态 | **已实现** |

## 1. 背景

OV3660 → 局域网浏览器零插件观看；与狗控同页。本阶段**不上 MediaMTX**（客户端推流见 [C02](../client/C02-device-push-stream.md)）。默认上电 **Off**。

## 2. 目标

可开关 MJPEG；`/stream` 不堵 `/api/cmd`。

## 3. 采集三态

| 模式 | API | 行为 |
|------|-----|------|
| Off | `off` | 不采帧（默认） |
| PeriodicSample | `periodic` | ≈1Hz CaptureOnly |
| Streaming | `stream` | ≈8fps → JPEG → `/stream` |

`/stream` 用独立 MJPEG 任务；并发满返回 503。

## 4. 接口

| 方法 | 路径 |
|------|------|
| GET | `/stream` |
| POST | `/api/capture_mode?mode=off\|periodic\|stream` |
| GET | `/api/status`（`mode`/`stream_clients`/`has_jpeg`/`port`） |

## 5. 验收

- [ ] 点「视频流」画面可辨；切 Off 降负
- [ ] 拉流时仍可 `/api/cmd`
- [ ] 多路拉流受限或 503，不看门狗复位

## 6. 不包含

人脸框（S03）；RTMP/公网推流（C02）。
