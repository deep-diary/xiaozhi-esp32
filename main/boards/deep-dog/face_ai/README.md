# deep-dog `face_ai`（人脸检测 + 本地数字 ID + Immich 真名）

## 作用

- 在 **MJPEG 视频流** 同一套采图路径上，把 **RGB565 紧密帧** 送入独立 FreeRTOS 任务。
- **S03**：`human_face_detect` 出框；浏览器 Canvas 轮询 `GET /api/face` 画框。
- **S04**：同任务内 `HumanFaceRecognizer` 做 embedding → 5s 会话去重 → facedb 1:N → 未命中自动分配数字 ID（`#1`…）。
- **S05**：对**尚无真名**的 `local_id`（含已有 `#1`/`#2`）异步裁剪上传 Immich → 写 NVS 真名；**默认保留**临时 asset（可配删除）。
- 与 **`http-server` 解耦**：HTTP 仅调 `face_ai_bridge.h` / `immich_client.h`，不在 httpd 回调里推理或上传。

## 数据流

1. `Streaming`：`CaptureOnly` → `PackedRgb565FromFrame` → `DeepDogFaceAiSubmitFrameIfDue` → JPEG → `/stream`。
2. `dog_face_ai`：检测 → 识别 →（门控）裁剪 JPEG → 投递 `dog_immich` worker。
3. `dog_immich`：`POST /assets` → 轮询 `people` → 绑名 →（可选）`DELETE /assets`。**不** `PUT /jobs`。
4. `GET /api/face`：含 `local_id`、`display_name`、`recognize_source`。

```text
有脸 → embedding → session / nvs / enrolled（display_name=#<id>）
  → 无有效真名且已配置 Immich？ → 异步 upload → 真名写 NVS
```

多人脸时按 score 识别最多 **`DEEP_DOG_FACE_RECOG_MULTI_MAX`（默认 4）** 张，每张可有独立 `local_id` / `display_name`（含 Immich 真名）。

## HTTP（S05）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/immich_config?api_key=...&api_url=...&delete_asset=0\|1` | Key / URL / 是否删临时图写入 NVS（`fdog_im`）；可只改 `delete_asset` |
| GET | `/api/immich_status` | 含 `delete_asset`、上次结果（无 Key 明文） |
| POST | `/api/face_refresh_name?local_id=` | 强制下次再取真名 |

联调图：[fixtures/ge_weidong.png](../swrs/vision/fixtures/ge_weidong.png)（葛维冬）。

## 分区与存储

| 分区 / NVS | 用途 |
|------------|------|
| `human_face_feat` | MFN 模型 |
| `facedb` | 特征库 |
| NVS `fdog_fr` | `local_id`、`display_name`(32)、`immich_person_id`…（meta_ver=2） |
| NVS `fdog_im` | Immich `api_url` / `api_key` / `del_asset` |

## 配置宏（`face_ai_config.h`）

- `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS`（联调默认 **1000**；与 MJPEG fps 独立）
- `DEEP_DOG_FACE_IMMICH_ENABLE`（默认 1）
- `DEEP_DOG_FACE_IMMICH_DEFAULT_URL`、`BACKOFF_S`（联调 **15**）、`POLL_MAX`（**60**）/ `POLL_MS`（**2000**，合计 ~120s）
- `DEEP_DOG_FACE_IMMICH_DELETE_ASSET`（默认 **0**=保留临时图）

分辨率与传感器格式在 board [`config.json`](../config.json) 配置（OV2640 / OV3660 切换与 RGB565/YUV422 说明见 [`../CAMERA_SENSOR.md`](../CAMERA_SENSOR.md)；**§9 双分辨率**见 [S06 §9](../swrs/vision/server/S06-higher-resolution.md#9-双分辨率剖面ov2640--640-采集--320-推流--待实现)）。

## 验收

- 无 Key：仍显示 `#id`
- 有 Key + Immich 已命名人物：数秒内 `#n` → 真名；已有无真名的 `#1` 也会触发
- 串口可见 upload / poll / bound；**无**主动 `job faceDetection` 日志；默认 `keep asset=`
- 仓库无 API Key 明文
