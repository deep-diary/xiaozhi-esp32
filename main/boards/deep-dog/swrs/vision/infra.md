# 共享基础设施（MediaMTX / EMQX / Immich）

> 视觉轨事实源。勿在各需求重复改地址。  
> **禁止**在本仓库写入真实 API Key / 登录密码（见 `mem/API密钥#Immich` 等）。

## 1. MediaMTX

主机：`192.168.31.25`

| 用途 | 地址 | 公网 |
|------|------|------|
| 内网推 RTMP | `rtmp://192.168.31.25:1935/<路径>` | ❌ |
| 内网推 RTSP | `rtsp://192.168.31.25:8554/<路径>` | ❌ |
| 内网 HLS | `http://192.168.31.25:8888/<路径>` | 局域网 |
| **外网 HLS** | `https://live.deep-diary.com/<路径>/index.m3u8` | ✅ |
| WebRTC 局域网 | `http://192.168.31.25:8889/<路径>` | 局域网 |

推荐：设备**仅内网推**；外网只拉 HLS。

| 用途 | 路径示例 |
|------|----------|
| deep-dog 主码流 | `deep-dog/<device_id>` |
| 联调 | `deep-dog/dev` |

## 2. EMQX

| 用途 | 地址 |
|------|------|
| 设备 MQTT 局域网 | `mqtt://192.168.31.25:1883`（**设备首选**） |
| 网页 MQTT WS 外网 | `wss://mqtt-ws.deep-diary.com/mqtt` |
| Dashboard | 局域网 `:18083` / `https://mqtt.deep-diary.com/` |

密码见密钥库；固件用 NVS。ESP 勿裸连公网 `mqtt-tcp.`（须 Access TCP）。

Topic 前缀建议：`deepdiary/deep-dog/<device_id>/...`

| Topic | 方向 | 关联 |
|-------|------|------|
| `stream/cmd` / `stream/status` | 双向 | [C03](./client/C03-mqtt-stream-control.md) |
| `gimbal/cmd` / `gimbal/status` | 双向 | [C04](./client/C04-mqtt-gimbal.md) |
| `person/active` | 设备→前端 | [P01](./product/P01-kiosk-personalization.md)（可选） |

## 3. Immich

| 环境 | API | 设备用途 |
|------|-----|----------|
| **本机（默认）** | `http://192.168.31.25:2283/api` | S05 上传裁剪图取真名 |
| 生产 | `http://im.deep-diary.com/api` | 可选；只读 Key **不可**作上传 |

Key 存 NVS；无同步搜人 API → 上传 asset → 轮询 person（[S05](./server/S05-immich-real-name.md)）。须先有 [S04](./server/S04-local-face-numeric-id.md) 去重。

## 4. 安全

- 禁止硬编码密钥。
- 人脸图默认只上局域网 Immich；不经公网 MQTT 传大图。
