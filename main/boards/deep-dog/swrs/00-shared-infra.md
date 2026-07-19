# 00 · 共享基础设施（MediaMTX / EMQX）

> 本文档为基础设施事实源，不单独作为固件交付项。后续需求文档引用本文，勿在各处重复改地址。

## 1. MediaMTX

主机：`192.168.31.25`

| 用途 | 地址 | 是否公网 |
|------|------|----------|
| 内网推流 RTMP | `rtmp://192.168.31.25:1935/<路径>` | ❌ 仅局域网 |
| 内网推流 RTSP | `rtsp://192.168.31.25:8554/<路径>` | ❌ 仅局域网 |
| 内网拉 HLS | `http://192.168.31.25:8888/<路径>` | 局域网 |
| **外网拉 HLS** | `https://live.deep-diary.com/<路径>/index.m3u8` | ✅ P0：Tunnel → `:8888`；P1 再经 Nginx |
| WebRTC 局域网 | `http://192.168.31.25:8889/<路径>` | 局域网 |
| WebRTC 规划外网 | `https://rtc.deep-diary.com/<路径>` | 可选 |

**推荐拓扑**

- 设备（ESP）**仅内网推** RTMP 或 RTSP。
- 外网观看**只拉 HLS**（`live.deep-diary.com`）。
- 本机开播脚本与 Hermes skill 见 `projects/homelab/live-stream`（仓库外文档）。

**路径约定（建议）**

| 用途 | 路径示例 | 说明 |
|------|----------|------|
| diary-brain 主码流 | `diary-brain/<device_id>` | `device_id` 与设备 MAC / 序列号对齐，避免多机冲突 |
| 调试临时流 | `diary-brain/dev` | 仅联调 |

## 2. EMQX

主机：`192.168.31.25`

| 用途 | 地址 | 是否公网 |
|------|------|----------|
| Dashboard 局域网 | `http://192.168.31.25:18083` | 局域网 |
| Dashboard 外网 | `https://mqtt.deep-diary.com/` | ✅ 仅 Web 控制台 |
| 设备 MQTT（TCP）局域网 | `mqtt://192.168.31.25:1883`（TLS `:8883`） | **设备首选** |
| 设备 MQTT（TCP）外网 | Tunnel：`mqtt-tcp.deep-diary.com` → `tcp://localhost:1883` | ✅ 须 `cloudflared access tcp` 本地转发 |
| 网页 MQTT（WebSocket） | 内网 `ws://192.168.31.25:8083/mqtt` → 外网 `wss://mqtt-ws.deep-diary.com/mqtt` | ✅ Tunnel → `:8083` |

**账号（局域网运维）**

- 用户名：`admin`
- 密码：见家庭实验室密钥管理（勿写入固件源码；设备侧用 NVS / 配网下发）

**路径与限制（摘要）**

1. **网页控内网设备**：浏览器 → `wss://mqtt-ws.deep-diary.com/mqtt` → Tunnel → EMQX `:8083` → 内网设备订 `1883` 同 topic。
2. **原生 MQTT TCP 外网（`mqtt-tcp.`）**
   - Cloudflare Public Hostname：**Type = TCP**，URL 指向 `localhost:1883`（或内网 broker）。
   - ESP **不能**直接写 `mqtt://mqtt-tcp.deep-diary.com:1883` 裸连；通常需 Access + `cloudflared access tcp`。
   - **推荐**：家里 ESP 直连 `192.168.31.25:1883`；`mqtt-tcp` 仅外出调试机使用。
3. **`mqtt-ws` 与 `mqtt-tcp` 勿混用**；Dashboard / 网页用 WS。
4. 公网暴露原生 MQTT 风险高于 WS：须认证 + ACL；不需要时关掉该 Hostname。
5. Tunnel 配置参考：`apps/homelab/cloudflared/config.example.yml` · `projects/homelab/README`（仓库外）。

## 3. Topic / 命名空间（跨需求统一）

前缀约定：`deepdiary/diary-brain/<device_id>/...`

| Topic（相对前缀） | 方向 | 用途 | 关联需求 |
|-------------------|------|------|----------|
| `stream/cmd` | 云 → 设备 | 推流开/关 | [03](./03-mqtt-stream-control.md) |
| `stream/status` | 设备 → 云 | 推流状态上报 | [03](./03-mqtt-stream-control.md) |
| `gimbal/cmd` | 云 → 设备 | 云台角度/相对位移 | [04](./04-mqtt-gimbal-control.md) |
| `gimbal/status` | 设备 → 云 | 云台当前位置 | [04](./04-mqtt-gimbal-control.md) |
| `face/event` | 设备 → 云 | 检测到人脸（可含上传完成引用） | [05](./05-face-detect-upload.md) |
| `face/result` | 云 → 设备 | Immich 识别结果 | [06](./06-face-recognize-immich.md) |
| `person/active` | 后台 → 前端 | 当前识别到的人物，驱动 Kiosk | [07](./07-kiosk-personalization.md) |

Payload 统一 JSON，字段在各需求文档细化；`device_id` 与推流路径一致。

## 4. 安全基线

- 固件**禁止**硬编码 broker 密码；开发期可用 Kconfig/菜单配置，量产走配网或 OTA 下发。
- MQTT ACL：按 `device_id` 限制可订阅/发布的 topic。
- 人脸图属敏感数据：上传走内网 HTTPS/HTTP；外网仅经已认证后台，不经公网 MQTT 传大图。
