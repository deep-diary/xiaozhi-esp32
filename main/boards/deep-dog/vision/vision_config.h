#ifndef _DEEP_DOG_VISION_CONFIG_H_
#define _DEEP_DOG_VISION_CONFIG_H_

/** 1=编译 VisionFrameHub + RTSP JPEG Push；0=仅保留旧 HTTP 采帧路径（不推荐） */
#ifndef DEEP_DOG_VISION_HUB_ENABLE
#define DEEP_DOG_VISION_HUB_ENABLE 1
#endif

/** 开机是否自动 RTSP 推流（默认关；C03 MQTT 再远程开） */
#ifndef DEEP_DOG_VISION_PUSH_AT_BOOT
#define DEEP_DOG_VISION_PUSH_AT_BOOT 1
#endif

/** MediaMTX RTSP 发布基址（不含 path） */
#ifndef DEEP_DOG_VISION_RTSP_HOST
#define DEEP_DOG_VISION_RTSP_HOST "192.168.31.25"
#endif
#ifndef DEEP_DOG_VISION_RTSP_PORT
#define DEEP_DOG_VISION_RTSP_PORT 8554
#endif

/** 流路径，对应 infra：deep-dog/<device_id>；联调用 deep-dog/dev */
#ifndef DEEP_DOG_VISION_STREAM_PATH
#define DEEP_DOG_VISION_STREAM_PATH "deep-dog/dev"
#endif

/**
 * 网页可播地址（外网 HLS）。MQTT stream/status.url 用此值，便于前端直接打开。
 * 设备仍向局域网 RTSP 推流；外网只拉 HLS（见 vision/infra.md）。
 */
#ifndef DEEP_DOG_VISION_PUBLIC_PLAY_URL
#define DEEP_DOG_VISION_PUBLIC_PLAY_URL \
    "https://live.deep-diary.com/" DEEP_DOG_VISION_STREAM_PATH "/index.m3u8"
#endif
/**
 * RTSP 推流编码：1=esp_h264 软编（默认，网页 HLS 前置）；0=回退 RTP/JPEG。
 * JPEG 路径仍保留编译，改此宏即可切回。
 */
#ifndef DEEP_DOG_VISION_CODEC_H264
#define DEEP_DOG_VISION_CODEC_H264 1
#endif

/** 推流目标帧率（Hub 在 RtspPush / HttpMjpeg 时共用上限；H.264 建议 3～5） */
#ifndef DEEP_DOG_VISION_PUSH_FPS
#if DEEP_DOG_VISION_CODEC_H264
#define DEEP_DOG_VISION_PUSH_FPS 3
#else
#define DEEP_DOG_VISION_PUSH_FPS 5
#endif
#endif

/** JPEG 质量（与历史 HTTP MJPEG 接近） */
#ifndef DEEP_DOG_VISION_JPEG_QUALITY
#define DEEP_DOG_VISION_JPEG_QUALITY 55
#endif

/** 断线重连：初始 / 最大退避 ms */
#ifndef DEEP_DOG_VISION_RECONNECT_MIN_MS
#define DEEP_DOG_VISION_RECONNECT_MIN_MS 1000
#endif
#ifndef DEEP_DOG_VISION_RECONNECT_MAX_MS
#define DEEP_DOG_VISION_RECONNECT_MAX_MS 30000
#endif

#endif  // _DEEP_DOG_VISION_CONFIG_H_
