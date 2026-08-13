# V-S06 · 预览/检测分辨率提升（240 → VGA）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S06** |
| 优先级 | P1 |
| 依赖 | [S02](./S02-http-mjpeg.md)、[S03](./S03-http-face-overlay.md)、[S05](./S05-immich-real-name.md) |
| 下一切片 | [C02 设备推流](../client/C02-device-push-stream.md)（或并行） |
| 代码落点 | `EspVideo` / `VisionFrameHub` 出图；`face_ai` 送帧与 Immich 上传；RTSP H.264 推流缩放 |
| 基线状态 | OV3660 **全 VGA 同分辨率**路径曾实现；**当前联调默认 OV2640 240×240**（见 §8）。**§9 双分辨率剖面**（640 采集 + 320 推流）已评估可行，**待实现** |

## 1. 背景与动机

当前深狗 HTTP 预览 / 人脸管线曾为 **240×240** RGB565。S05 联调结论：

- 主机用清晰原图 [`fixtures/ge_weidong.png`](../fixtures/ge_weidong.png)（约 331×452）上传 Immich → 可识别 **葛维冬**。
- 设备 **240×240 翻拍/预览帧**（含网页截图、相机对着照片）上传 Immich → 长期 `people=[]`，画面只保留 `#序号`。
- 主机将原图长边缩到 240（`176×240`）→ Immich 仍 `people=[]`（负例见 [`ge_weidong_long240.jpg`](../fixtures/ge_weidong_long240.jpg)）。

因此高度怀疑 **像素过低**（脸部有效区域常不足百像素级）是 Immich 真名失败的主因之一。

本切片目标：把**传感器出图 / 人脸检测 / Immich 上传**提高到 Immich/InsightFace 更友好的档位；**推流**在带宽与 H.264 软编约束下可低于采集分辨率（见 **§9**）。

历史实选（OV3660，全链路同分辨率 VGA）：

| 项 | 值 |
|----|-----|
| Kconfig | `CONFIG_CAMERA_OV3660_DVP_RGB565_640X480_10FPS` |
| 分辨率 | **640×480**（VGA，短边 480 ≥ RES-04 的 320） |
| 相对旧 240² | 像素量约 **×5.3** |
| 标称帧率 | 10 fps |

**当前硬件默认**为 SparkBot **OV2640**（见 board `config.json`）；推荐下一剖面为 **§9 双分辨率**（640 采集 + 320 推流），而非全链路 640×480 推流。

## 2. 目标

| ID | 需求 |
|----|------|
| RES-01 | Streaming 时传感器输出 **640×480**（已写明实选模式名；非严格 480²） |
| RES-02 | `/stream` MJPEG 与控制页预览按新分辨率显示（Canvas / `vidWrap` 跟 `/api/face` 的 `w`/`h`） |
| RES-03 | `face_ai` 检测与识别输入与预览同分辨率 |
| RES-04 | Immich 上传裁剪最短边 ≥ **320**（`DEEP_DOG_FACE_IMMICH_MIN_CROP_PX`）；失败仍降级 `#id` |
| RES-05 | 拉流时仍可 `/api/cmd`；不因分辨率升高导致看门狗复位或常驻 OOM |
| RES-06 | **（§9 待实现）** 传感器 **640×480** 采集；`face_ai` / Immich 用全分辨率帧 |
| RES-07 | **（§9 待实现）** RTSP H.264 推流前软件缩至 **320×240**（QVGA）；编码输入与 MediaMTX/HLS 分辨率为 320×240 |

## 3. 非目标（本切片不做）

- 不改为异步画框同步方案（框滞后属 S03 架构；可另开切片）。
- 不强制上 720p/1080p。
- 不改 Immich 服务端模型。
- 不为方形 480 自研裁切管线。
- **运行时**在 240 与 640 间热切换传感器模式（Kconfig 编译期二选一）。
- §9 阶段不把 RTSP 也提到 640×480 软编（易 OOM/WDT，见 [`CAMERA_SENSOR.md`](../../../CAMERA_SENSOR.md)）。

## 4. 影响面

| 模块 | 注意 |
|------|------|
| `EspVideo` / OV2640 或 OV3660 | OV2640：`640X480_6FPS`（§9）；OV3660：`640X480_10FPS`（历史全 VGA） |
| `VisionFrameHub` | 单路采帧；§9 在 RTSP 分支 **640→320 RGB565 缩放** 再 H.264；face 仍用原帧 |
| MJPEG | HTTP 预览可与采集同分辨率或另行缩放（§9 以 RTSP 为主） |
| `face_ai` | VGA 一帧 RGB565 ≈ **614KB**（240² 仅 ≈115KB）；队列拷贝占 PSRAM；`MIN_INTERVAL_MS` 宜 500～1000 |
| RTSP / H.264 | 推流 **320×240@3～5fps**；`h264_sw_encoder` 建议输入 ≤320 宽 |
| 控制页 / MQTT | `face/status` 的 `w`/`h` 反映**采集**分辨率（640×480）；流分辨率见 stream 文档 |
| Immich | `MIN_CROP_PX=320`；VGA 下 `MAX_CROP_PX` 宜抬至 **480**；临时 asset **默认保留**（S05） |
| 内存 | ESP32-S3 + **8MB PSRAM**：§9 评估**工程可行**；须实机观察 internal min free（Immich 并发时曾低至 ~3KB） |

## 5. 建议验收

- [x] `/api/face` 中 `w`/`h` 为 **640 / 480**
- [x] 网页预览 Canvas 跟 `j.w`/`j.h`；MJPEG 实测帧 **640×480**
- [ ] 对准 [`ge_weidong.png`](../fixtures/ge_weidong.png) 或真人：数秒内有机会出现 Immich 真名（允许偶发 unknown）
  - 注：2026-07-20 验收时 Immich `faceDetection` 队列积压数万，主机原图探针亦超时无 `people`；设备侧已上传 crop≥320，待队列消化后复验真名
- [x] 用旧 240 截图 / [`ge_weidong_long240.jpg`](../fixtures/ge_weidong_long240.jpg) 作对照：低像素 Immich 易失败（已知）
- [x] Streaming + 人脸开 + 拉流时 `/api/cmd` 可用；VGA 下降载：`MIN_INTERVAL=500`、MJPEG 5fps/q60、双 MJPEG worker、send 超时 2s
- [ ] **§9 双分辨率（OV2640）**：`/api/face` `w/h`=640/480；Immich crop 短边 ≥320；RTSP/HLS 实测 **320×240**；internal min 不持续 <4KB、无 WDT
- [ ] **§9**：对准 [`ge_weidong.png`](../fixtures/ge_weidong.png) 或真人，Immich 真名成功率优于 240×240 基线（允许偶发 unknown）

## 6. 风险

- PSRAM/内部 RAM 与编码带宽；必要时 `config.json` 回切 `RGB565_240X240_24FPS`。
- 检测帧间隔可能需加大（现 `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS=250`）。
- 传感器标称 10fps，MJPEG 目标 fps 宜 ≤8 以免积压。

## 7. 相关

- Immich 契约：[S05](./S05-immich-real-name.md)
- 现状 MJPEG：[S02](./S02-http-mjpeg.md)
- 联调图：[fixtures/README](../fixtures/README.md)

## 8. 分辨率切换与联调模式

传感器出图尺寸由 board **`config.json` → `sdkconfig_append`** 选定（**编译期**；改完需 `idf.py build` / 烧录）。

### 8.1 当前默认（OV2640 · 240²）

| 模式 | Kconfig | 用途 |
|------|---------|------|
| **联调（当前默认）** | `CONFIG_CAMERA_OV2640_DVP_RGB565_240X240_25FPS=y` | 降内存 / 降 WDT；Immich 上传 crop≈240，真名成功率偏低 |

| 项 | 值 |
|----|-----|
| 采集 / face / Immich | 240×240 |
| RTSP H.264 | 240×240 @ ~3～5fps |
| 检测间隔 | `MIN_INTERVAL_MS=1000` |
| Immich 失败退避 | `BACKOFF_S=15` |

### 8.2 历史剖面（OV3660 · 全 VGA）

| 模式 | Kconfig | 用途 |
|------|---------|------|
| 全链路 VGA | `CONFIG_CAMERA_OV3660_DVP_RGB565_640X480_10FPS` | 采集=推流=检测 640×480；Immich crop≥320 |

### 8.3 流帧率与人脸检测

MJPEG / RTSP 由 `VisionFrameHub` 节流；检测由 `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS` 节流；MQTT `face/status` 与检测同频。**三者可不同步**；§9 另增「采集分辨率 ≠ 推流分辨率」。

---

## 9. 双分辨率剖面（OV2640 · 640 采集 + 320 推流 · 待实现）

### 9.1 结论（2026-08 评估）

在 **OV2640 + ESP32-S3 + 8MB PSRAM** 当前硬件上：

> **640×480 传感器采集 + 320×240 RTSP 推流降级** — 工程上**可行**，作为 S06 下一增量优于「全链路 240²」或「全链路 640 推流」。

依据：

| 维度 | 说明 |
|------|------|
| 传感器 | OV2640 已有 `CONFIG_CAMERA_OV2640_DVP_RGB565_640X480_6FPS`（board `config.json` 已预留，`=n`） |
| 架构 | `VisionFrameHub` 单路采帧 → face 用原图；RTSP 分支**仅推流前** RGB565 缩至 320×240，无需双传感器 |
| Immich | 全 VGA 帧上 crop 短边可达 **320～480**，对齐 S05；解决联调中 `crop=240×240` 导致 Immich `people=[]` |
| H.264 | 320×240 软编 CPU/内存远低于 640×480；与 `h264_sw_encoder`「建议 ≤320 宽」一致 |
| 风险 | PSRAM 每帧 +~500KB、internal RAM 在 Immich 并发时偏紧 — **须 Phase 实机压测**，不保证零改动 |

**本阶段只更新需求文档，不写固件。**

### 9.2 目标架构

```text
OV2640 RGB565 640×480 @ ~6fps
        │
        ├─► face_ai（检测 / 识别 / Immich JPEG crop）— 全分辨率
        │
        └─► VisionFrameHub RTSP 路径
              RGB565 downscale 640×480 → 320×240
              └─► esp_h264 SW @ 3～5fps → MediaMTX → HLS
```

| 路径 | 分辨率 | 帧率建议 |
|------|--------|----------|
| 传感器 / `face/status` `w×h` | **640×480** | 传感器 ~6fps；检测 `MIN_INTERVAL_MS` 500～1000 |
| RTSP / HLS 输出 | **320×240** | 3～5fps（`DEEP_DOG_VISION_PUSH_FPS`） |
| Immich 上传 crop | 短边 **≥320**，上限宜 **480** | 随检测触发，非每帧 |

### 9.3 Kconfig 与配置项（规划）

| 项 | 规划值 |
|----|--------|
| 传感器 | `CONFIG_CAMERA_OV2640_DVP_RGB565_640X480_6FPS=y`，240 模式 `=n` |
| 推流缩放 | `DEEP_DOG_VISION_STREAM_W=320`，`DEEP_DOG_VISION_STREAM_H=240`（Hub 内实现，宏名待代码落地） |
| face | `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS` 500～1000 |
| Immich | `DEEP_DOG_FACE_IMMICH_MIN_CROP_PX=320`，`MAX_CROP_PX=480` |
| 回退 | 内存/WDT 不达标 → 回 §8.1 240² 或仅关 RTSP |

### 9.4 实现阶段（ROADMAP 勾选用）

| Phase | 内容 | 产出 |
|-------|------|------|
| **0** | 本文 §9 + ROADMAP / C02 索引 | ✅ 文档 |
| **1** | `config.json` 切 640×480_6FPS；调 face 间隔与 Immich crop 宏 | 编译通过 |
| **2** | `VisionFrameHub`：RTSP 前 RGB565 640→320 缩放 | HLS 320×240 |
| **3** | 实机：SystemInfo internal min、Immich 真名、RTSP ≥10min | 验收 §5 新项 |

### 9.5 验收（§9 专用）

- [ ] `face/status` / registry 侧帧尺寸 **640×480**
- [ ] RTSP `DESCRIBE` / 拉流落盘分辨率为 **320×240**
- [ ] Immich 上传 crop 短边 **≥320**（串口 `immich jpeg ready crop=…`）
- [ ] 真人或 [`ge_weidong.png`](../fixtures/ge_weidong.png)：真名成功率 **优于** 240² 基线
- [ ] 推流 + 人脸 + Immich 并发：无 WDT；internal `min` 不持续低于 **~4KB**（或记录实测下限后修订）
- [ ] `stream/cmd` 关推流后，face 仍可用全 VGA 静默检测

### 9.6 相关

- 推流客户端：[C02](../client/C02-device-push-stream.md)
- Immich：[S05](./S05-immich-real-name.md)
- 传感器切换说明：[`CAMERA_SENSOR.md`](../../../CAMERA_SENSOR.md) § OV2640 VGA
