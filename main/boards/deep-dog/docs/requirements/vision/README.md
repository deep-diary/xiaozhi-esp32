# vision · 视觉与流媒体

交付顺序（权威）：**Server（S01～S06）→ Client（C01～C02）→ MQTT（M01 + modules / V-C03～C04）→ Product（P01）**。

| 子目录 | 角色 |
|--------|------|
| [server/](./server/) | 设备 = HTTP 服务器 |
| [client/](./client/) | 设备 = 流媒体客户端（推 MediaMTX） |
| [../mqtt/](../mqtt/) | 板级 MQTT（入口卡 + 模块详情页规格） |
| [product/](./product/) | Kiosk / 对话个性化 |
| [infra.md](./infra.md) | MediaMTX / EMQX / Immich |

总表与勾选：[../ROADMAP.md](../ROADMAP.md)

```text
浏览器 ──► HTTP :8080
          ├─ /api/cmd、/stream、/api/face   ← server S01～S05
          └─ face_ai（检测 → 数字 ID → Immich）
推流客户端 ──► MediaMTX                     ← client C02（复用 face_ai）
```
