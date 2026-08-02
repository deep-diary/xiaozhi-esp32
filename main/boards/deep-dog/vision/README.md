# vision/

设备视觉帧调度与 MediaMTX 客户端推流（C01/C02）。

## 架构

- `VisionFrameHub`：统一采帧；人脸永驻送 `face_ai`；视频发布二选一
  - `off`：不推流，静默人脸
  - `mjpeg`：局域网 HTTP `/stream`
  - `rtsp_push`：RTSP 客户端推 MediaMTX（**默认 H.264**）
- `RtspH264Pusher` + `H264SwEncoder`（`esp_h264` 软编）：SDP `H264/90000`，RTP 单 NAL / FU-A
- `RtspJpegPusher`：保留；`DEEP_DOG_VISION_CODEC_H264=0` 可回退 RTP/JPEG

配置见 [`vision_config.h`](./vision_config.h)。板级在 `StartNetwork` 启动 `face_ai` + Hub；HTTP 仅控制页/`/api`。

## API（HTTP，C03 MQTT 将复用同一状态机）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/vision_publish?mode=off\|stream\|rtsp_push` | 切换发布（与 `/api/capture_mode` 同源） |
| GET | `/api/status` | 含 `mode`、`push_status`、`push_url` |

默认开机不推流（`DEEP_DOG_VISION_PUSH_AT_BOOT=0`）。默认 URL：`rtsp://192.168.31.25:8554/deep-dog/dev`。H.264 默认约 3fps、240×240。

## 联调 / 验收

```bash
# 开推流
curl -X POST 'http://192.168.31.211:8080/api/vision_publish?mode=rtsp_push'

# Python 拉流落盘（硬门禁；本机 ffmpeg 连 :8554 可能 EHOSTUNREACH，脚本会走 TCP 拉流）
python3 scripts/deep_dog/deep_dog_rtsp_pull_verify.py --outdir h264 --python-only
# JPEG 回退固件时：
python3 scripts/deep_dog/deep_dog_rtsp_pull_verify.py --outdir jpeg --python-only

# DESCRIBE 期望 H264/90000
# HLS（软探，非硬门禁）：http://192.168.31.25:8888/deep-dog/dev/index.m3u8
```

产物目录：[`swrs/vision/fixtures/rtsp_pull_verify/`](../swrs/vision/fixtures/rtsp_pull_verify/)。

说明：JPEG RTSP **不能**被 MediaMTX 转成可用 HLS；H.264 为网页 HLS 前置。推流与人脸同机时若卡顿/OOM，可降人脸间隔或优先关人脸。
