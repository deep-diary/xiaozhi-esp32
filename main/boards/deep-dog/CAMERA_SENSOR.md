# deep-dog 摄像头传感器说明（OV2640 / OV3660）

本文说明 **ESP-SparkBot 兼容硬件** 上两种常见 DVP 传感器的差异、固件如何配置，以及视觉/人脸管线对像素格式的假设。

> **结论先行**
> - 传感器型号与默认像素格式在 **编译期** 由 `config.json` → `sdkconfig` 决定，**不能** 在同一份固件里运行时切换 OV2640 ↔ OV3660。
> - **OV2640**（SparkBot 原装）：官方参考为 **YUV422 240×240@25fps**；也可出 RGB565，但字节序需单独验证。
> - **OV3660**（部分深狗/拇指板）：联调常用 **RGB565**，并常配合 `CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP`。
> - deep-dog 视觉栈下游（H.264 软编、人脸检测）统一消费 **packed RGB565 LE**；传感器若出 YUV422，会在 `VisionFrameHub` 里多一步转换。

---

## 1. 硬件与引脚（两种传感器相同）

deep-dog 与 `esp-sparkbot` 共用 DVP + SCCB 引脚，定义在 [`config.h`](config.h)：

| 信号 | GPIO | 说明 |
|------|------|------|
| SCCB (I2C) | 4 / 5 | 与 ES8311 共用 I2C0，摄像头 init 时复用同一 bus |
| XCLK | 15 | LEDC 输出，当前 **16 MHz**（与 SparkBot 一致） |
| PCLK / VSYNC / HSYNC | 13 / 6 / 7 | DVP 同步 |
| D0–D7 | 11,9,8,10,12,18,17,16 | 8bit 并行数据 |

**屏幕与摄像头无关**：LCD 走 SPI（ST7789），不占用摄像头 I2C；背光 GPIO46 与功放 PA 复用，见板级 `esp_sparkbot_board.cc` 注释。

---

## 2. 传感器对比

| 项目 | OV2640（SparkBot / 当前 deep-dog 默认） | OV3660（历史 deep-dog / deep-thumble） |
|------|----------------------------------------|----------------------------------------|
| 典型模组 | ESP-SparkBot 原装 200 万像素 | 部分定制深狗、拇指形态板 |
| 官方/参考固件 | [`esp-sparkbot/config.json`](../esp-sparkbot/config.json) → **YUV422** 240×240@25fps | [`deep-thumble/config.json`](../deep-thumble/config.json) → **RGB565** 240×240@24fps |
| Kconfig 格式选项 | RGB565 / YUV422 / JPEG 等多档（240×240、640×480…） | RGB565 / YUV422 / JPEG（240×240、640×480…） |
| 推荐 XCLK | 16 MHz（SparkBot）；驱动表内也有 20M 时序 | 常配 20 MHz（高分辨率档） |
| **字节序修正** | 传感器侧：`CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER`（**仅 OV2640 等**，S3 默认关） | 无上述 Kconfig；用 **`CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP`**（`EspVideo` 采帧后软件 swap） |
| 误配症状 | PID 读失败、`/dev/video2` 不存在 | 同上 |
| 格式误配症状 | 结构正常但 **花屏/伪色**（YUV 当 RGB565 或字节序反） | RGB565 未 swap 时偏色/块状 |

---

## 3. YUV422 → RGB565 多一道转换，吃资源吗？

**会，但 240×240 下通常可接受。**

当前实现：[`vision/vision_frame_hub.cc`](vision/vision_frame_hub.cc) 中 `YuyvToRgb565Packed()`，用 `esp_imgfx_color_convert`（BT601）在采帧后转换。

| 维度 | 240×240 量级估算 |
|------|------------------|
| 单帧 buffer | 输入/输出各约 **115 KB**（240×240×2） |
| CPU | ESP32-S3 上 imgfx 色彩转换约 **数 ms/帧**（与主频、PSRAM 有关） |
| 调用频率 | 与 `VisionFrameHub` 采帧一致（推流 fps 上限 + 人脸间隔，非每个 CPU 周期） |
| 内存 | 转换结果写入 `std::vector`，与 H.264 编码、人脸任务分时复用 PSRAM |

**何时值得省掉这一步？**

- 传感器 **原生 RGB565** 且 **字节序已验证正确** → `CapturePackedRgb565()` 走 `PackedRgb565FromFrame()` 直传，无 imgfx 转换。
- 若仅 MJPEG 对外、且保持 YUV422：JPEG 编码器 [`image_to_jpeg`](../display/lvgl_display/jpg/image_to_jpeg.cpp) 可直接吃 YUYV；但 **H.264 软编**（`H264SwEncoder::EncodeRgb565`）和 **人脸检测** 仍要 RGB565，故 deep-dog 统一在 Hub 里转成 RGB565。

**优化方向（未做）**：H.264 路径可改为 YUYV→I420 直转，跳过 RGB565 中间态；需改 `h264_sw_encoder`，与人脸路径仍要 RGB565。

---

## 4. OV2640 能否像 OV3660 一样直接出 RGB565？

**可以。** Kconfig 选项：

```text
CONFIG_CAMERA_OV2640_DVP_RGB565_240X240_25FPS=y
# CONFIG_CAMERA_OV2640_DVP_YUV422_240X240_25FPS=n
```

但需注意：

1. **SparkBot 官方验证路径是 YUV422**，不是 RGB565；直接改 RGB565 可能遇到 **字节序** 问题（此前 deep-dog 用 RGB565 未配对 swap 时出现花屏）。
2. OV2640 优先尝试 **`CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER`**（传感器寄存器内 swap，见 `esp_cam_sensor` Kconfig 说明）。
3. 若仍偏色，再试 **`CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP=y`**（`EspVideo` 在 `CaptureOnly` 里对每 16bit 像素 bswap；**对 YUV422 有害**，仅 RGB565 时启用）。
4. 确认正确后，`VisionFrameHub` 无需 YUYV 分支即可工作（已有 `V4L2_PIX_FMT_RGB565` 分支）。

**建议策略**

| 场景 | 推荐 |
|------|------|
| SparkBot 兼容 / 与上游 esp-sparkbot 对齐 | OV2640 + **YUV422** + Hub 转 RGB565（颜色最稳） |
| **deep-dog 默认（省 CPU）** | OV2640 + **RGB565** + `CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER`（当前 `config.json`） |
| 追求少一次转换、已实机验证颜色 | OV2640 + **RGB565** + 配对 byte swap |
| OV3660 模组 | OV3660 + **RGB565** + `ENDIANNESS_SWAP`（参考 deep-thumble） |

---

## 5. 切换传感器：要改什么？

传感器切换 = **改板级配置 + 全量重新编译 + 烧录**。没有 NVS/ MQTT 运行时切换。

### 5.1 改 [`config.json`](config.json) 的 `sdkconfig_append`

**OV2640 + YUV422（SparkBot 默认，当前 deep-dog）**

```json
"CONFIG_CAMERA_OV3660=n",
"CONFIG_CAMERA_OV2640=y",
"CONFIG_CAMERA_OV2640_AUTO_DETECT_DVP_INTERFACE_SENSOR=y",
"CONFIG_CAMERA_OV2640_DVP_YUV422_240X240_25FPS=y",
"CONFIG_CAMERA_OV2640_DVP_RGB565_240X240_25FPS=n"
```

**OV2640 + RGB565（需实机验证 swap）**

```json
"CONFIG_CAMERA_OV2640_DVP_RGB565_240X240_25FPS=y",
"CONFIG_CAMERA_OV2640_DVP_YUV422_240X240_25FPS=n",
"CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER=y"
```

按需追加 `"CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP=y"`（仅 RGB565；**不要**与 YUV422 同开）。

**OV3660 + RGB565（deep-thumble 剖面）**

```json
"CONFIG_CAMERA_OV2640=n",
"CONFIG_CAMERA_OV3660=y",
"CONFIG_CAMERA_OV3660_AUTO_DETECT_DVP_INTERFACE_SENSOR=y",
"CONFIG_CAMERA_OV3660_DVP_YUV422_240X240_24FPS=n",
"CONFIG_CAMERA_OV3660_DVP_RGB565_240X240_24FPS=y",
"CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP=y"
```

高分辨率 Immich 剖面见 [`swrs/vision/server/S06-higher-resolution.md`](swrs/vision/server/S06-higher-resolution.md)（OV3660 640×480 RGB565）。

### 5.2 其他可能调整项

| 文件 / 宏 | OV2640 | OV3660 |
|-----------|--------|--------|
| [`config.h`](config.h) `SPARKBOT_CAMERA_XCLK_FREQ` | 16 MHz（SparkBot） | 可试 20 MHz（高分辨率） |
| [`config.json`](config.json) | 见上 | 见上 |
| `sdkconfig` | `idf.py build` 会合并；或 `python scripts/release.py deep-dog` | 同左 |
| [`vision_frame_hub.cc`](vision/vision_frame_hub.cc) | YUV422 时走 `YuyvToRgb565Packed`；RGB565 直 pack | 通常 RGB565 直 pack；YUV422 同左 |
| [`face_ai_config.h`](face_ai/face_ai_config.h) | 默认 RGB565 进检测；偏色可调 `DEEP_DOG_FACE_DETECT_RGB565_SWAP` | OV3660 RGB565 常需配合 `ENDIANNESS_SWAP` |
| 文档 | 本文件、`vision/README.md`、`face_ai/README.md` | S06 分辨率说明 |

**不必改**：DVP 引脚、`esp_sparkbot_board.cc` 里 `InitializeCamera()` 结构（除非换非 SparkBot 引脚定义的其他板）。

### 5.3 编译与验证

```bash
# 推荐：按 config.json 完整重配（release 脚本会 append sdkconfig）
python scripts/release.py deep-dog

# 或日常增量
idf.py build flash monitor
```

**验收日志**

- 成功：`EspVideo: Camera init success`；无 `Get sensor ID failed`
- 格式：查 `VIDIOC_G_FMT` / debug 下 FOURCC（YUYV / RGBP）
- 画面：流媒体无花屏；人脸框位置正常

**验收图像**

- 纯色块 / 肤色正常 → 格式与字节序正确
- 紫绿黄块状 → 格式或 endian 错误（典型：YUV 当 RGB565，或 RGB565 未 swap）

---

## 6. 软件数据流（与格式相关）

```text
传感器 (Kconfig 选 RGB565 或 YUV422)
    ↓ DVP + esp_video (V4L2)
EspVideo::CaptureOnlyTo → CameraFrame (format=1/2/3…)
    ↓
VisionFrameHub::CapturePackedRgb565
    ├─ RGB565 → PackedRgb565FromFrame
    └─ YUYV/YUV422P → YuyvToRgb565Packed (esp_imgfx)
    ↓ packed RGB565 LE
    ├─ H264SwEncoder::EncodeRgb565 → RTSP
    ├─ image_to_jpeg (v4l_fmt) → MJPEG
    └─ DeepDogFaceAiSubmitFrameIfDue → 人脸检测
```

[`EspVideo`](../common/esp_video.cc) 在 `CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP` 开启时，对采帧副本做 **16bit 字节交换**（适用于 RGB565 / YUV422P 路径，**开启后 YUV422 可能反而损坏**——故 YUV422 剖面应关闭此项）。

---

## 7. 常见问题

**Q：能否一份固件同时支持 OV2640 和 OV3660，上电自动识别？**  
A：当前 Kconfig 可同时 `=y` 多个 sensor 驱动，但 **DVP 上只会挂一颗**；`AUTO_DETECT` 按已启用驱动去匹配 PID。生产环境应 **只启用与实际硬件一致** 的那一颗，避免误检或体积浪费。

**Q：从 OV3660 改 OV2640 后为什么要改格式，不能只改传感器名？**  
A：3660 联调 RGB565 + swap 与 2640 SparkBot 参考 YUV422 是两套已验证组合；只改传感器名、保留 3660 的 RGB565+swap 配置在 2640 上极易花屏。

**Q：ENDIANNESS_SWAP 和 CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER 区别？**  
A：前者在 **CPU 采帧后**软件 swap（xiaozhi `EspVideo`）；后者在 **OV2640 传感器寄存器**内 swap。OV3660 无后者，用前者。

---

## 8. 推流目标（内网 / 外网）

路径 **`deep-dog/{device_id}`** 与 MQTT Topic 前缀中的 `device_id` 一致：未绑定为 `dev`，已绑定为 MAC 紧凑串。运行时由 [`mqtt/mqtt_config.cc`](mqtt/mqtt_config.cc) 生成 URL。

| 角色 | 地址（示例） | 定义位置 |
|------|------|----------|
| **设备 RTSP 推流（内网）** | `rtsp://192.168.31.25:8554/deep-dog/dev` 或 `…/deep-dog/{mac}` | `DeepDogMqttConfig::RtspPushUrlForDeviceId` |
| **局域网 HLS 播放** | `http://192.168.31.25:8888/deep-dog/{device_id}/index.m3u8` | `LanHlsUrlForDeviceId` |
| **外网 HLS 播放** | `https://live.deep-diary.com/deep-dog/{device_id}/index.m3u8` | `PublicHlsUrlForDeviceId`；MQTT `stream/status.url` |
| **MQTT 上报** | `push_url`=内网 RTSP，`url`=外网 HLS | [`mqtt/modules/stream_mqtt.cc`](mqtt/modules/stream_mqtt.cc) |

**结论：设备只向内网 MediaMTX 推 RTSP**；外网 HLS 由服务器侧转发，设备不直连公网推流。见 [`swrs/vision/infra.md`](swrs/vision/infra.md)。

---

## 9. 当前 deep-dog 默认剖面（SparkBot OV2640）

[`config.json`](config.json) 默认剖面（省 imgfx 转换、H.264 可推流）：

```json
"CONFIG_CAMERA_OV2640_DVP_RGB565_240X240_25FPS=y",
"CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER=y"
```

试验 VGA（640×480）时 H.264 软编易 OOM，见联调记录；可改：

```json
"CONFIG_CAMERA_OV2640_DVP_RGB565_640X480_6FPS=y",
"CONFIG_CAMERA_OV2640_DVP_RGB565_240X240_25FPS=n"
```

烧录后若花屏：关 `CAMERA_SENSOR_SWAP`、改开 `ENDIANNESS_SWAP`；仍不行则回退 YUV422（§5.1）。

---

## 10. 参考链接（仓库内）

| 路径 | 内容 |
|------|------|
| [`../esp-sparkbot/config.json`](../esp-sparkbot/config.json) | OV2640 YUV422 官方参考 |
| [`../deep-thumble/config.json`](../deep-thumble/config.json) | OV3660 RGB565 + ENDIANNESS_SWAP |
| [`config.json`](config.json) | deep-dog 当前传感器 Kconfig 片段 |
| [`vision/vision_frame_hub.cc`](vision/vision_frame_hub.cc) | YUYV→RGB565 转换 |
| [`swrs/vision/server/S06-higher-resolution.md`](swrs/vision/server/S06-higher-resolution.md) | OV3660 640×480 剖面 |
| [`../deep-thumble/docs/face-detect-esp-who-vs-current-detail.md`](../deep-thumble/docs/face-detect-esp-who-vs-current-detail.md) | YUV vs RGB565 与人脸检测 |

---

*文档版本：与 deep-dog OV2640 RGB565 240×240 + SENSOR_SWAP 及 device_id 动态 path 对齐。*
