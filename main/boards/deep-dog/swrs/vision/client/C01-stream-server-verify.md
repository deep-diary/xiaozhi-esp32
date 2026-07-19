# V-C01 · MediaMTX 推拉验收（无板）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C01** |
| 依赖 | [infra](../infra.md) |
| 下一切片 | [C02](./C02-device-push-stream.md) |
| 板端改动 | **无** |

## 目标

PC 完成「推流 → 内网拉 → 外网 HLS」，排除服务器故障与板端混淆。

## 验收

- [ ] ffmpeg/OBS 推 `rtmp://192.168.31.25:1935/deep-dog/dev` 成功
- [ ] 内网 HLS / 外网 `https://live.deep-diary.com/deep-dog/dev/index.m3u8` 可播 ≥30s
- [ ] 路径与 [infra](../infra.md) 一致，供 C02 复用

```bash
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=15 \
  -c:v libx264 -preset ultrafast -tune zerolatency -f flv \
  rtmp://192.168.31.25:1935/deep-dog/dev
```
