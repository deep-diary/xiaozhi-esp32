# vision · 视觉与流媒体

交付顺序（权威）：**Server（S01～S05）→ Client（C01～C04）→ Product（P01）**。

| 子目录 | 角色 |
|--------|------|
| [server/](./server/) | 设备 = HTTP 服务器（遥控、MJPEG、人脸框、本地 ID、Immich） |
| [client/](./client/) | 设备 = 流媒体客户端（推 MediaMTX、MQTT） |
| [product/](./product/) | Kiosk / 对话个性化 |
| [infra.md](./infra.md) | MediaMTX / EMQX / Immich 地址与安全约定 |

总表与勾选：[../ROADMAP.md](../ROADMAP.md)

```text
浏览器 ──► HTTP :8080
          ├─ /api/cmd、/stream、/api/face   ← server S01～S05
          └─ face_ai（检测 → 数字 ID → Immich）
推流客户端 ──► MediaMTX                     ← client C02（复用 face_ai）
```
