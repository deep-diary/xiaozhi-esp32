# V-C01 · MediaMTX 推拉验收（无板）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C01** |
| 依赖 | [infra](../infra.md) |
| 下一切片 | [C02](./C02-device-push-stream.md) |
| 板端改动 | **无** |
| 状态 | 基建可达；完整 ≥30s 推拉请在稳定局域网复验 |

## 目标

PC 完成「推流 → 内网拉 → 外网 HLS」，排除服务器故障与板端混淆。

## 验收

- [x] 主机 `192.168.31.25` ICMP 可达；`:8888` 返回 `Server: mediamtx`（空路径 404 属正常）
- [x] 外网入口 `https://live.deep-diary.com/...` 可达（空流 404 属正常）
- [ ] ffmpeg/OBS 推 `rtmp://192.168.31.25:1935/deep-dog/dev` 成功并 HLS ≥30s（本机曾出现端口 SYN 通、应用层偶发 `EHOSTUNREACH`，请在稳定 LAN 复验）
- [ ] **C02 同路径**：RTSP+JPEG 推 `rtsp://192.168.31.25:8554/deep-dog/dev` 可转 HLS（MediaMTX 官方支持 MJPEG/JPEG）
- [x] 路径与 [infra](../infra.md) 一致，供 C02 复用：`deep-dog/dev`

### RTMP（文档原命令）

```bash
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=15 \
  -c:v libx264 -preset ultrafast -tune zerolatency -t 35 -f flv \
  rtmp://192.168.31.25:1935/deep-dog/dev
```

### RTSP + JPEG（对齐 C02）

```bash
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=5 \
  -c:v mjpeg -q:v 5 -t 35 -f rtsp -rtsp_transport tcp \
  rtsp://192.168.31.25:8554/deep-dog/dev
```

播放：

- 内网：`http://192.168.31.25:8888/deep-dog/dev/index.m3u8`
- 外网：`https://live.deep-diary.com/deep-dog/dev/index.m3u8`
