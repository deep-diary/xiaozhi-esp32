# V-C02 · 设备作流媒体客户端：推流到 MediaMTX

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C02** |
| 优先级 | P1（**排在 S04/S05 之后**） |
| 依赖 | [C01](./C01-stream-server-verify.md)、[S04](../server/S04-local-face-numeric-id.md)（人脸复用）；建议 S05 已可用 |
| 代码落点 | `main/boards/deep-dog/` 推流模块；**复用** `face_ai`，不重开一套识别 |
| 验收 | 内网/外网可拉到设备相机；识别状态机与 HTTP Server 路径共用 |

## 1. 背景

先在 Server 路径（S01～S05）打通采帧与人脸，再让设备作为 **RTMP/RTSP 客户端** 推 MediaMTX。拓扑：设备内网推，外网只拉 HLS。

## 2. 目标

- 联网后可推流（默认开/关可配）。
- 路径 `deep-dog/<device_id>`（见 [infra](../infra.md)）。
- **人脸检测/数字 ID/Immich 编排与 S04/S05 同一实现**；推流只增加编码与网络发送。

## 3. 范围

**包含**：采帧→编码→推 RTMP/RTSP；失败退避；与语音共存降级。  
**不包含**：MQTT 开关（C03）；公网直推；另写第二套 face 模型。

## 4. 与 face_ai 复用

```text
EspVideo 帧源
  ├─ HTTP Streaming worker（S02/S03）— 设备作服务器时
  ├─ MediaMTX push worker（本需求）— 设备作客户端时
  └─ face_ai 状态机（S04/S05）← 同源帧，单实例
```

首版可要求「有活跃帧源」才跑识别；Server MJPEG 与 Client 推流模式可切换，避免双开满载。

## 5. 功能需求

| ID | 需求 |
|----|------|
| PUSH-01 | 可配置开机推流 |
| PUSH-02 | 路径/URL 可配 |
| PUSH-03 | 断网可恢复 |
| PUSH-04 | 不拖垮语音；冲突时降帧 |
| PUSH-05 | 识别逻辑调用既有 face_ai，不复制模型 |

## 6. 验收

- [ ] HLS/内网可看到相机画面 ≥10 分钟稳定
- [ ] 推流开启时，本地数字 ID（及真名若 S05）行为与 Server 路径一致或文档说明差异
- [ ] 代码主要在 `deep-dog/`，未为大改公共协议栈

## 7. 选型提示

首选调研 RTMP+H.264；分辨率/帧率优先稳定（如 ≤640×480、5～15fps）。
