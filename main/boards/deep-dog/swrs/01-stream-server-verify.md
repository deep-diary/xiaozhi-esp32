# 01 · 流媒体服务验收（不结合硬件）

| 项 | 内容 |
|----|------|
| 优先级 | P0 |
| 依赖 | [00 共享基础设施](./00-shared-infra.md) |
| 产出 | 确认 MediaMTX 推拉链路可用的操作记录 / 脚本 |
| 板端改动 | **无** |

## 1. 背景

在改固件之前，先验证家庭实验室 MediaMTX 与外网 HLS Tunnel 正常，避免设备联调时混淆「服务器挂了」与「板端推流失败」。

## 2. 目标

- 用 PC / 手机工具完成一次完整「推流 → 内网拉 → 外网 HLS 拉」。
- 固定一条用于 diary-brain 的测试路径（如 `diary-brain/dev`）。

## 3. 范围

**包含**

- RTMP 或 RTSP 推到 `192.168.31.25`
- 内网 HLS / WebRTC 拉流抽检
- 外网 `https://live.deep-diary.com/<路径>/index.m3u8` 拉流抽检

**不包含**

- ESP 固件、相机驱动、MQTT

## 4. 功能需求

| ID | 需求 | 说明 |
|----|------|------|
| SRV-01 | 内网可推 | 用 ffmpeg / OBS 向 `rtmp://192.168.31.25:1935/diary-brain/dev`（或 RTSP 等价路径）推流成功 |
| SRV-02 | 内网可拉 | 内网浏览器或 VLC 可打开 HLS / WebRTC 预览 |
| SRV-03 | 外网可拉 | 外网可打开 `https://live.deep-diary.com/diary-brain/dev/index.m3u8`（或当前 Tunnel 映射路径） |
| SRV-04 | 断流可观测 | 停止推流后，拉流端在合理时间内无画面或提示离线（记录实际延迟，供后续 UI 参考） |

## 5. 验收标准

- [ ] 书面记录：推流命令、路径、内网 URL、外网 URL、一次成功截图或日志时间戳
- [ ] 外网 HLS 连续观看 ≥ 30s 无明显长时间卡死（偶发卡顿可接受，记下大概码率）
- [ ] 路径命名与 [00](./00-shared-infra.md) 约定一致，可供 [02](./02-camera-stream-upload.md) 直接复用

## 6. 参考命令（示例，以实际 MediaMTX 配置为准）

```bash
# 示例：PC 推 RTMP 测试流
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=15 \
  -f lavfi -i sine=frequency=1000 -c:v libx264 -preset ultrafast -tune zerolatency \
  -c:a aac -f flv rtmp://192.168.31.25:1935/diary-brain/dev
```

外网播放器打开：`https://live.deep-diary.com/diary-brain/dev/index.m3u8`
