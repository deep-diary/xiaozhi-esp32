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
| **设备 MQTT 联调默认** | `mqtt://broker.emqx.io:1883`（公共站，匿名、无 TLS；**勿当生产**） |
| 设备 MQTT 局域网（可选） | `mqtt://192.168.31.25:1883`（NVS 可覆盖） |
| 网页 / ingest WSS | `wss://broker.emqx.io:8084/mqtt`（Hub 与云端 worker；与设备公共站 **同一** broker） |
| Dashboard | 局域网 `:18083` / `https://mqtt.deep-diary.com/` |

密码见密钥库；固件用 NVS。自建公网 `mqtt-tcp.` 勿裸连（须 Access TCP）。公共站 `broker.emqx.io:1883` 为当前联调路径，设备需能解析公网 DNS。Topic `deepdiary/deep-dog/dev/` 无鉴权可被他人订阅。

Topic 前缀：`deepdiary/deep-dog/<device_id>/...`  
**字段真源**：[mqtt/protocol/deep-dog-mqtt.yml](../mqtt/protocol/deep-dog-mqtt.yml)（需求 [M01](../mqtt/M01-board-mqtt-protocol.md)）。

| Topic | 方向 | 关联 |
|-------|------|------|
| `device/info` / `device/status` | ↑ | [M01](../mqtt/M01-board-mqtt-protocol.md) |
| `stream/cmd` / `stream/status` | 双向 | [02-stream](../mqtt/modules/02-stream.md)（V-C03） |
| `face/cmd` / `face/status` | 双向 | [04-face](../mqtt/modules/04-face.md) / M01 |
| `dog/cmd` / `dog/status` | 双向 | [07-dog](../mqtt/modules/07-dog.md) |
| `imu/status` | ↑ | [03-imu](../mqtt/modules/03-imu.md) / D9 |
| `led/cmd` / `led/status` | 双向 | [08-led](../mqtt/modules/08-led.md) |
| `servo/cmd` / `servo/status` | 双向 | [10-servo](../mqtt/modules/10-servo.md) |
| `gimbal/cmd` / `gimbal/status` | 双向 | [09-gimbal](../mqtt/modules/09-gimbal.md)（V-C04） |
| `handle/cmd` / `handle/input` / `handle/status` | 双向 | [11-handle](../mqtt/modules/11-handle.md)；架构 [input/](../input/) |
| `touch/status` | ↑ | [06-touch](../mqtt/modules/06-touch.md) |
| `can/cmd` / `can/status` / `can/frames` / `can/tx` | 双向 | [12-can](../mqtt/modules/12-can.md) |
| `person/active` | ↑ | [13-person](../mqtt/modules/13-person.md)（预留） |
| `track/cmd` / `track/status` | 双向 | [05-track](../mqtt/modules/05-track.md)（MQTT ready / actuator=none） |

## 3. Immich

| 环境 | API | 设备用途 |
|------|-----|----------|
| **本机（默认）** | `http://192.168.31.25:2283/api` | S05 上传裁剪图取真名 |
| 生产 | `http://im.deep-diary.com/api` | 可选；只读 Key **不可**作上传 |

Key 存设备 NVS（`fdog_im`），经 `POST /api/immich_config` 下发；**禁止**写入本仓库。无同步搜人 API → 上传临时 asset → 轮询 `people`（**不**主动 `PUT /jobs`）→ 绑名；临时 asset **默认保留**（`delete_asset=0`），可配删除（[S05](./server/S05-immich-real-name.md)）。须先有 [S04](./server/S04-local-face-numeric-id.md) 去重。

联调图：[`fixtures/ge_weidong.png`](./fixtures/ge_weidong.png)（葛维冬）。旧 **240²** Immich 易失败；S06 **§9** 规划 640 采集 + 320 推流（OV2640，评估可行，待实现），见 [S06 §9](./server/S06-higher-resolution.md)。

## 4. 安全

- 禁止硬编码密钥。
- 人脸图默认只上局域网 Immich；不经公网 MQTT 传大图。
