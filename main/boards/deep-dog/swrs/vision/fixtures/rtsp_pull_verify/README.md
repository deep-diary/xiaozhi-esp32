# rtsp_pull_verify

由 `scripts/deep_dog_rtsp_pull_verify.py` 生成的拉流验收产物。

```bash
# 默认固件为 H.264
python3 scripts/deep_dog_rtsp_pull_verify.py --outdir h264 --python-only

# JPEG 回退固件（DEEP_DOG_VISION_CODEC_H264=0）
python3 scripts/deep_dog_rtsp_pull_verify.py --outdir jpeg --python-only
```

说明：本机 `ffmpeg` 直连 MediaMTX `:8554` 可能出现 `EHOSTUNREACH`；脚本用 Python RTSP/TCP 拉流，再用本机 ffmpeg 解封装/抽帧。

产物示例：`h264/pull_5s.h264`、`h264/frame_001.jpg`；`jpeg/pull_5s.mp4`、`jpeg/frame_001.jpg`。
