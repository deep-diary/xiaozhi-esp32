# V-S06 · 预览/检测分辨率提升（240 → 480）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S06** |
| 优先级 | P1 |
| 依赖 | [S02](./S02-http-mjpeg.md)、[S03](./S03-http-face-overlay.md)、[S05](./S05-immich-real-name.md) |
| 下一切片 | [C02 设备推流](../client/C02-device-push-stream.md)（或并行） |
| 代码落点 | OV3660 / `EspVideo` 出图尺寸；`http-server` 预览与 Canvas；`face_ai` 送帧与 Immich 上传 |
| 基线状态 | **待办**（仅需求；实现另排） |

## 1. 背景与动机

当前深狗 HTTP 预览 / 人脸管线实测为 **240×240** RGB565。S05 联调结论：

- 主机用清晰原图 [`fixtures/ge_weidong.png`](../fixtures/ge_weidong.png)（约 331×452）上传 Immich → 可识别 **葛维冬**。
- 设备 **240×240 翻拍/预览帧**（含网页截图、相机对着照片）上传 Immich → 长期 `people=[]`，画面只保留 `#序号`。

因此高度怀疑 **像素过低**（脸部有效区域常不足百像素级）是 Immich 真名失败的主因之一；同时低分辨率也会加重「框与画面不同步」时的观感问题。

本切片目标：把**传感器出图 / MJPEG / 检测共用分辨率**线性约 **加大一倍**：**240×240 → 480×480**（像素量约 ×4）。

## 2. 目标

| ID | 需求 |
|----|------|
| RES-01 | Streaming 时传感器（或管线）输出 **480×480**（允许 OV3660 最接近的方形档，须在文档写明实选模式名） |
| RES-02 | `/stream` MJPEG 与控制页预览按新分辨率显示（Canvas / `vidWrap` 与归一化框坐标一致） |
| RES-03 | `face_ai` 检测与识别输入与预览同分辨率（或明确「检测用缩小副本」策略并验收延迟） |
| RES-04 | Immich 上传图的短边建议 ≥ **320**（整帧 480 或加大裁剪 padding）；失败仍降级 `#id` |
| RES-05 | 拉流时仍可 `/api/cmd`；不因分辨率升高导致看门狗复位或常驻 OOM |

## 3. 非目标（本切片不做）

- 不改为异步画框同步方案（框滞后属 S03 架构；可另开切片）。
- 不强制上 720p/1080p。
- 不改 Immich 服务端模型。

## 4. 影响面（实现时核对）

| 模块 | 注意 |
|------|------|
| `EspVideo` / OV3660 | 选 480 档；XCLK、带宽、初始化稳定性 |
| MJPEG | JPEG 体积↑；可略降 `jpeg_quality_` 或 `stream_target_fps_` 保实时 |
| `face_ai` | RGB565 一帧约 240²×2≈112KB → 480²×2≈450KB；队列拷贝与推理耗时 |
| 控制页 | `vidWrap` / canvas 默认尺寸、`drawFaces` 不再写死 240 |
| Immich | 上传更大 JPEG；仍用后删除临时 asset（S05） |

## 5. 建议验收

- [ ] `/api/face` 中 `w`/`h` 为 480（或文档记载的实选分辨率）
- [ ] 网页预览清晰度明显高于 240；两人场景框坐标与脸对齐（归一化）
- [ ] 对准 [`ge_weidong.png`](../fixtures/ge_weidong.png) 或真人：数秒内有机会出现 Immich 真名（允许偶发 unknown）
- [ ] 用旧 240 截图 [`camera_view_couple.png`](../fixtures/camera_view_couple.png) 作对照：主机 Immich 仍可能失败（已知低像素局限）
- [ ] Streaming + 人脸开 + 拉流时狗控按钮仍可用；串口无反复 OOM

## 6. 风险

- PSRAM/内部 RAM 与编码带宽；必要时默认 480、保留宏回退 240。
- 检测帧间隔可能需加大（现 `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS=250`）。
- OV3660 若无严格 480×480，取最接近方形并更新本文与 fixtures 说明。

## 7. 相关

- Immich 契约：[S05](./S05-immich-real-name.md)
- 现状 MJPEG：[S02](./S02-http-mjpeg.md)
- 联调图：[fixtures/README](../fixtures/README.md)
