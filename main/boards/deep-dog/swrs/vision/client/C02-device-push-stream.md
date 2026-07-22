# V-C02 · 设备作流媒体客户端：推流到 MediaMTX

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C02** |
| 优先级 | P1 |
| 依赖 | [C01](./C01-stream-server-verify.md)、[S04](../server/S04-local-face-numeric-id.md)；建议 S05 已可用 |
| 代码落点 | [`main/boards/deep-dog/vision/`](../../../vision/)；**复用** `face_ai` |
| 状态 | **固件已实现**（IDF 5.5.2 编译通过）；板端长稳 / HLS 联调待实机勾选 |

## 1. 背景

先在 Server 路径（S01～S05）打通采帧与人脸，再让设备作为 **RTSP 客户端** 推 MediaMTX（JPEG over RTP）。拓扑：设备内网推，外网只拉 HLS。

## 2. 目标

- 联网后可推流（默认**关**，`DEEP_DOG_VISION_PUSH_AT_BOOT`）。
- 路径 `deep-dog/<device_id>`（联调默认 `deep-dog/dev`，见 [infra](../infra.md)）。
- **人脸检测/数字 ID/Immich 与 S04/S05 同一实现**；推流只增加编码与网络发送。
- **人脸管线永驻**（与推流开关无关）；MJPEG Server 与 RTSP Client **互斥**。

## 3. 范围

**包含**：采帧→JPEG→推 RTSP；失败退避；与 MJPEG 互斥；HTTP `/api/vision_publish`。  
**不包含**：MQTT 开关（C03）；公网直推；另写第二套 face 模型。

## 4. 与 face_ai 复用

```text
EspVideo 帧源
  └─ VisionFrameHub
       ├─ face_ai（永驻，默认开）
       ├─ HTTP MJPEG /stream（mode=stream）
       └─ RTSP JPEG Push（mode=rtsp_push）
```

## 5. 功能需求

| ID | 需求 | 实现 |
|----|------|------|
| PUSH-01 | 可配置开机推流 | `DEEP_DOG_VISION_PUSH_AT_BOOT`（默认 0） |
| PUSH-02 | 路径/URL 可配 | `vision_config.h` / `SetRtspUrl` |
| PUSH-03 | 断网可恢复 | 指数退避重连 |
| PUSH-04 | 不拖垮语音；冲突时降帧 | 默认 5fps；与 MJPEG 互斥 |
| PUSH-05 | 识别逻辑调用既有 face_ai | Hub → `SubmitFrameIfDue` |

## 6. 验收

- [ ] HLS/内网可看到相机画面 ≥10 分钟稳定
- [ ] 推流开启时，本地数字 ID（及真名若 S05）行为与 Server 路径一致
- [x] 代码主要在 `deep-dog/vision/` + HTTP API 接线，未为大改公共协议栈
- [x] `mode=off` 时人脸仍可跑（静默发现人）
- [x] `mode=stream` 与 `mode=rtsp_push` 互斥

## 7. 选型

首版：**RTSP + JPEG（RTP/JPEG）**，复用 `image_to_jpeg`；分辨率/帧率优先稳定（≤640×480、约 5fps）。
