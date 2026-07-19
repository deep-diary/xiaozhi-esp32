# deep-dog `face_ai`（人脸检测 + 本地数字 ID）

## 作用

- 在 **MJPEG 视频流** 同一套采图路径上，把 **RGB565 紧密帧** 送入独立 FreeRTOS 任务。
- **S03**：`human_face_detect` 出框；浏览器 Canvas 轮询 `GET /api/face` 画框。
- **S04**：同任务内 `HumanFaceRecognizer` 做 embedding → 5s 会话去重 → facedb 1:N → 未命中自动分配数字 ID（`#1`…）；**不调 Immich**。
- 与 **`http-server` 解耦**：HTTP 仅调 `face_ai_bridge.h`，不在 httpd 回调里推理。

## 数据流

1. `Streaming`：`CaptureOnly` → `PackedRgb565FromFrame` → `DeepDogFaceAiSubmitFrameIfDue` → JPEG → `/stream`。
2. `dog_face_ai`：检测（保留 keypoints）→（可选）`DeepDogFaceRecognizeProcess` → `DeepDogFaceSnapshot`。
3. `GET /api/face`：含 `local_id`、`display_name`、`recognize_source`（`session`/`nvs`/`enrolled`/`none`）及 `faces[]`。
4. `POST /api/face_enable?enabled=0|1`：开关检测+识别（关则停识别，库保留）。

```text
有脸 → embedding
  → 5s 会话相似？ → source=session
  → DataBase 命中？ → source=nvs
  → 未满员 → enroll → source=enrolled（display_name=#<id>）
```

多人脸时只识别 **score 最高** 一张（NID-06）。

## 分区与存储

见 `partitions/v2/16m_deep_dog.csv`：

| 分区 | 用途 |
|------|------|
| `human_face_feat` (2MB) | MFN 特征模型（`CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_PARTITION`） |
| `facedb` (512KB FAT) | `HumanFaceRecognizer` 特征库文件 `/facedb/db` |
| NVS `fdog_fr` | 仅元数据：`local_id`、`display_name`、`updated_at`、`immich_person_id`（S05 预留） |

NVS 仅 16KB，装不下官方 ~2KB/人的向量；`assets` 每次刷 generated_assets 会被覆盖，故不用作特征库。

改分区后需 **全量 flash**（含 partition table + `human_face_feat` 模型分区）。

## 依赖

- `human_face_detect`、`esp-dl`（managed_components）
- `human_face_recognition`：**vendored** 于仓库根目录 [`components/human_face_recognition`](../../../../components/human_face_recognition)（Registry 0.3.2 仍锁 detect ~0.4；本拷贝对齐 detect ~0.5）。`main/idf_component.yml` 用 `path: ../components/human_face_recognition`。

## 配置宏（`face_ai_config.h`）

- `DEEP_DOG_FACE_AI_ENABLE` / `DEEP_DOG_FACE_RECOG_ENABLE`
- `DEEP_DOG_FACE_RECOG_MAX`（默认 16）、`SESSION_MS`（5000）、`SIM_THR`（0.5）
- 检测阈值 / 暗场门控等同前（默认 score 0.5；勿再抬到 0.88）

## 验收（S04）

- 不同人 → 不同 `local_id`
- 同一人连续出镜 → ID 稳定（session 或 nvs）
- 串口无 Immich HTTP
- 重启后已建档成员仍回原 `local_id`

## 后续

- **S05**：为已有 `local_id` 绑定 Immich 真名（写 NVS `display_name` / `immich_person_id`）。
- **C02**：推流复用同一套 `face_ai` 状态。
