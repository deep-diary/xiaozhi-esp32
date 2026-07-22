# vision/

设备视觉帧调度与 MediaMTX 客户端推流（C01/C02）。

## 架构

- `VisionFrameHub`：统一采帧；人脸永驻送 `face_ai`；视频发布二选一
  - `off`：不推流，静默人脸
  - `mjpeg`：局域网 HTTP `/stream`
  - `rtsp_push`：RTSP+JPEG 推 MediaMTX
- `RtspJpegPusher`：RTSP ANNOUNCE/SETUP/RECORD + RTP/JPEG（TCP interleaved）

配置见 [`vision_config.h`](./vision_config.h)。板级在 `StartNetwork` 启动 `face_ai` + Hub；HTTP 仅控制页/`/api`。

## API（HTTP，C03 MQTT 将复用同一状态机）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/vision_publish?mode=off\|stream\|rtsp_push` | 切换发布（与 `/api/capture_mode` 同源） |
| GET | `/api/status` | 含 `mode`、`push_status`、`push_url` |

默认开机不推流（`DEEP_DOG_VISION_PUSH_AT_BOOT=0`）。默认 URL：`rtsp://192.168.31.25:8554/deep-dog/dev`。

## 联调

```bash
# 开推流
curl -X POST 'http://<device-ip>:8080/api/vision_publish?mode=rtsp_push'
# 关推流（人脸仍跑）
curl -X POST 'http://<device-ip>:8080/api/vision_publish?mode=off'
# 拉 HLS（infra）
# http://192.168.31.25:8888/deep-dog/dev/index.m3u8
# https://live.deep-diary.com/deep-dog/dev/index.m3u8
```
