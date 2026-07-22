# V-S06 · 预览/检测分辨率提升（240 → VGA）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S06** |
| 优先级 | P1 |
| 依赖 | [S02](./S02-http-mjpeg.md)、[S03](./S03-http-face-overlay.md)、[S05](./S05-immich-real-name.md) |
| 下一切片 | [C02 设备推流](../client/C02-device-push-stream.md)（或并行） |
| 代码落点 | OV3660 / `EspVideo` 出图尺寸；`http-server` 预览与 Canvas；`face_ai` 送帧与 Immich 上传 |
| 基线状态 | **已实现**（VGA 路径可用）；**联调默认回切 240×240** 降内存，见 §8 |

## 1. 背景与动机

当前深狗 HTTP 预览 / 人脸管线曾为 **240×240** RGB565。S05 联调结论：

- 主机用清晰原图 [`fixtures/ge_weidong.png`](../fixtures/ge_weidong.png)（约 331×452）上传 Immich → 可识别 **葛维冬**。
- 设备 **240×240 翻拍/预览帧**（含网页截图、相机对着照片）上传 Immich → 长期 `people=[]`，画面只保留 `#序号`。
- 主机将原图长边缩到 240（`176×240`）→ Immich 仍 `people=[]`（负例见 [`ge_weidong_long240.jpg`](../fixtures/ge_weidong_long240.jpg)）。

因此高度怀疑 **像素过低**（脸部有效区域常不足百像素级）是 Immich 真名失败的主因之一。

本切片目标：把**传感器出图 / MJPEG / 检测共用分辨率**提高到 Immich/InsightFace 更友好的档位。OV3660 DVP RGB565 **无原生 480×480**，实选：

| 项 | 值 |
|----|-----|
| Kconfig | `CONFIG_CAMERA_OV3660_DVP_RGB565_640X480_10FPS` |
| 分辨率 | **640×480**（VGA，短边 480 ≥ RES-04 的 320） |
| 相对旧 240² | 像素量约 **×5.3** |
| 标称帧率 | 10 fps |

## 2. 目标

| ID | 需求 |
|----|------|
| RES-01 | Streaming 时传感器输出 **640×480**（已写明实选模式名；非严格 480²） |
| RES-02 | `/stream` MJPEG 与控制页预览按新分辨率显示（Canvas / `vidWrap` 跟 `/api/face` 的 `w`/`h`） |
| RES-03 | `face_ai` 检测与识别输入与预览同分辨率 |
| RES-04 | Immich 上传裁剪最短边 ≥ **320**（`DEEP_DOG_FACE_IMMICH_MIN_CROP_PX`）；失败仍降级 `#id` |
| RES-05 | 拉流时仍可 `/api/cmd`；不因分辨率升高导致看门狗复位或常驻 OOM |

## 3. 非目标（本切片不做）

- 不改为异步画框同步方案（框滞后属 S03 架构；可另开切片）。
- 不强制上 720p/1080p。
- 不改 Immich 服务端模型。
- 不为方形 480 自研裁切管线。

## 4. 影响面

| 模块 | 注意 |
|------|------|
| `EspVideo` / OV3660 | `config.json` 切 640×480@10fps；XCLK 20MHz |
| MJPEG | JPEG 体积↑；默认 `jpeg_quality_=60`、`stream_target_fps_=5` |
| `face_ai` | RGB565 一帧约 640×480×2≈**600KB**；`MIN_INTERVAL_MS=500`；Immich crop 上限 400、VGA 不整帧回退 |
| 控制页 | `vidWrap` / canvas 跟 `j.w`/`j.h`，不再写死 240 |
| Immich | `MIN_CROP_PX=320`；临时 asset **默认保留**（S05 `delete_asset=0`） |

## 5. 建议验收

- [x] `/api/face` 中 `w`/`h` 为 **640 / 480**
- [x] 网页预览 Canvas 跟 `j.w`/`j.h`；MJPEG 实测帧 **640×480**
- [ ] 对准 [`ge_weidong.png`](../fixtures/ge_weidong.png) 或真人：数秒内有机会出现 Immich 真名（允许偶发 unknown）
  - 注：2026-07-20 验收时 Immich `faceDetection` 队列积压数万，主机原图探针亦超时无 `people`；设备侧已上传 crop≥320，待队列消化后复验真名
- [x] 用旧 240 截图 / [`ge_weidong_long240.jpg`](../fixtures/ge_weidong_long240.jpg) 作对照：低像素 Immich 易失败（已知）
- [x] Streaming + 人脸开 + 拉流时 `/api/cmd` 可用；VGA 下降载：`MIN_INTERVAL=500`、MJPEG 5fps/q60、双 MJPEG worker、send 超时 2s

## 6. 风险

- PSRAM/内部 RAM 与编码带宽；必要时 `config.json` 回切 `RGB565_240X240_24FPS`。
- 检测帧间隔可能需加大（现 `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS=250`）。
- 传感器标称 10fps，MJPEG 目标 fps 宜 ≤8 以免积压。

## 7. 相关

- Immich 契约：[S05](./S05-immich-real-name.md)
- 现状 MJPEG：[S02](./S02-http-mjpeg.md)
- 联调图：[fixtures/README](../fixtures/README.md)

## 8. 分辨率切换与联调模式

传感器出图尺寸由 board **`config.json` → `sdkconfig_append`** 二选一（改完需 `idf.py build` / 烧录；也可同步改仓库根 `sdkconfig`）：

| 模式 | Kconfig（`=y`，另一项 `=n`） | 用途 |
|------|------------------------------|------|
| **联调（当前默认）** | `CONFIG_CAMERA_OV3660_DVP_RGB565_240X240_24FPS` | 降内存 / 降 WDT；Immich 真名成功率偏低 |
| Immich / 高像素复验 | `CONFIG_CAMERA_OV3660_DVP_RGB565_640X480_10FPS` | 约 ×5.3 像素；需配合更长检测间隔 |

**流帧率与人脸检测本就异步**：MJPEG 由 `stream_target_fps_` 节流；检测由 `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS` 节流；网页框由 `setInterval(pollFace, …)` 轮询 `/api/face`。三者可不同步。

当前联调默认（`face_ai_config.h` + `http-server`）：

| 项 | 值 |
|----|-----|
| 分辨率 | 240×240 |
| 检测间隔 | `MIN_INTERVAL_MS=1000` |
| Immich 失败退避 | `BACKOFF_S=15` |
| 页面 `/api/face` | 500ms |
| MJPEG | ~4fps / q55 |
