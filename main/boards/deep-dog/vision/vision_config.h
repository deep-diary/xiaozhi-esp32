#ifndef _DEEP_DOG_VISION_CONFIG_H_
#define _DEEP_DOG_VISION_CONFIG_H_

#include "config.h"

/** 1=编译 VisionFrameHub + RTSP JPEG Push；默认见 board_features.h */
#ifndef DEEP_DOG_VISION_HUB_ENABLE
#define DEEP_DOG_VISION_HUB_ENABLE 1
#endif

/** 开机是否自动 RTSP 推流（默认关；stream/cmd 再远程开） */
#ifndef DEEP_DOG_VISION_PUSH_AT_BOOT
#define DEEP_DOG_VISION_PUSH_AT_BOOT 0
#endif

/** MediaMTX RTSP 发布基址（不含 path） */
#ifndef DEEP_DOG_VISION_RTSP_HOST
#define DEEP_DOG_VISION_RTSP_HOST "192.168.31.25"
#endif
#ifndef DEEP_DOG_VISION_RTSP_PORT
#define DEEP_DOG_VISION_RTSP_PORT 8554
#endif

/**
 * 流路径（对应 infra：`deep-dog/<device_id>`）。
 * REQ-IOT-310 §6：**默认按设备唯一编码（STA MAC）生成**，与公布的 RTSP/HLS URL、MediaMTX path 一致。
 * 若部署要求固定路径（如 MediaMTX 未开 `all_others`、只放行 `deep-dog/dev`），
 * 在板级/构建配置里**显式定义** `DEEP_DOG_VISION_STREAM_PATH` 覆盖。
 */

/**
 * 注：外网/局域网播放地址不再用编译期常量拼装（那会与设备标识脱钩）；
 * 统一由 `DeepDogMqttConfig::PublicHlsUrlForDeviceId()` / `RtspPushUrlForDeviceId()` 按 device_id 生成，
 * 见 `mqtt/mqtt_config.cc`。
 */
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
#define DEEP_DOG_VISION_PUSH_FPS 5
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

/** S06 §9：RTSP H.264 推流分辨率（采集可为 640×480，推流前缩放） */
#ifndef DEEP_DOG_VISION_STREAM_W
#define DEEP_DOG_VISION_STREAM_W 320
#endif
#ifndef DEEP_DOG_VISION_STREAM_H
#define DEEP_DOG_VISION_STREAM_H 240
#endif

/** RTSP 推流最长时长（秒）；超时自动 stop 并恢复语音（V-S08） */
#ifndef DEEP_DOG_STREAM_RTSP_MAX_S
#define DEEP_DOG_STREAM_RTSP_MAX_S 300
#endif

#endif  // _DEEP_DOG_VISION_CONFIG_H_
