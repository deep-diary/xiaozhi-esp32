# deep-dog `face_ai`（人脸检测 + 本地数字 ID + Immich 真名）

## 作用

- 在 **MJPEG 视频流** 同一套采图路径上，把 **RGB565 紧密帧** 送入独立 FreeRTOS 任务。
- **S03**：`human_face_detect` 出框；浏览器 Canvas 轮询 `GET /api/face` 画框。
- **S04**：同任务内 `HumanFaceRecognizer` 做 embedding → 5s 会话去重 → facedb 1:N → 未命中自动分配数字 ID（`#1`…）。
- **S05**：对**尚无真名**的 `local_id`（含已有 `#1`/`#2`）异步裁剪上传 Immich → 写 NVS 真名；临时 asset 用后即删。
- 与 **`http-server` 解耦**：HTTP 仅调 `face_ai_bridge.h` / `immich_client.h`，不在 httpd 回调里推理或上传。

## 数据流

1. `Streaming`：`CaptureOnly` → `PackedRgb565FromFrame` → `DeepDogFaceAiSubmitFrameIfDue` → JPEG → `/stream`。
2. `dog_face_ai`：检测 → 识别 →（门控）**优先整帧 JPEG** → 投递 `dog_immich` worker。
3. `dog_immich`：`POST /assets` → 触发 face jobs → 轮询 `people` → 绑名 → `DELETE /assets`。
4. `GET /api/face`：含 `local_id`、`display_name`、`recognize_source`。

```text
有脸 → embedding → session / nvs / enrolled（display_name=#<id>）
  → 无有效真名且已配置 Immich？ → 异步 upload → 真名写 NVS
```

多人脸时按 score 识别最多 **`DEEP_DOG_FACE_RECOG_MULTI_MAX`（默认 4）** 张，每张可有独立 `local_id` / `display_name`（含 Immich 真名）。

## HTTP（S05）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/immich_config?api_key=...&api_url=...` | Key 写入 NVS（`fdog_im`）；url 可省略 |
| GET | `/api/immich_status` | 是否已配置、上次结果（无 Key 明文） |
| POST | `/api/face_refresh_name?local_id=` | 强制下次再取真名 |

联调图：[fixtures/ge_weidong.png](../swrs/vision/fixtures/ge_weidong.png)（葛维冬）。

## 分区与存储

| 分区 / NVS | 用途 |
|------------|------|
| `human_face_feat` | MFN 模型 |
| `facedb` | 特征库 |
| NVS `fdog_fr` | `local_id`、`display_name`(32)、`immich_person_id`…（meta_ver=2） |
| NVS `fdog_im` | Immich `api_url` / `api_key` |

## 配置宏（`face_ai_config.h`）

- `DEEP_DOG_FACE_IMMICH_ENABLE`（默认 1）
- `DEEP_DOG_FACE_IMMICH_DEFAULT_URL`、`BACKOFF_S`、`POLL_MAX` / `POLL_MS`

## 验收

- 无 Key：仍显示 `#id`
- 有 Key + Immich 已命名人物：数秒内 `#n` → 真名；已有无真名的 `#1` 也会触发
- 串口可见 upload / poll / delete / bound 日志
- 仓库无 API Key 明文
