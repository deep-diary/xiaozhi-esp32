# V-S05 · Immich 真名绑定到本地数字 ID

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S05** |
| 优先级 | P1 |
| 依赖 | [S04](./S04-local-face-numeric-id.md)、[infra Immich](../infra.md) |
| 下一切片 | [C02 设备推流](../client/C02-device-push-stream.md)（或先做 C01） |
| 代码落点 | `face_ai/immich_client.*` + runtime 门控；不在 httpd 回调上传 |
| 基线状态 | 已实现（设备侧需对准 Immich 已命名正脸验收真名） |

## 1. 背景

S04 已用数字 ID 区分人。本阶段在**已有或新建 local_id** 上，按需直连**局域网 Immich** 取真名并回写 NVS。失败时仍显示数字 ID。

## 2. Immich 约束

无同步「上传即返回姓名」API。契约（对齐 xiaozhi-server，**不主动触发 jobs**）：

```text
裁剪 JPEG → POST /assets → 轮询 GET /assets/{id} 的 people[] → 绑 name + person_id
         → （可选）DELETE 临时 asset；默认不删，便于在 Immich 网页核对
```

延迟秒级～数十秒（Immich ML 队列，**非** HTTP 超时）；设备轮询上限见 `DEEP_DOG_FACE_IMMICH_POLL_MAX` × `POLL_MS`（默认 **60×2000ms≈120s**）。超时后保留 asset 并 `name_pending=1`，每 `DEFERRED_POLL_S`（默认 300s）重试 poll。须配合 S04 去重，禁止 1Hz 狂刷。详见 [infra](../infra.md)。

Immich 开启 Facial Recognition 后，**上传本身会入队**检测/识别；设备**不得** `PUT /jobs/faceDetection|facialRecognition`，以免误伤全局队列或已命名库。

Immich 2.x `GET /assets/{id}` 的人物在 **`people[]`**（含 `name` / `id`），勿只等顶层 `faces`。

## 3. 何时调用

需要远程命名，当且仅当：

```text
local_id > 0
AND Immich 已配置（NVS 有 api_key）
AND （尚无有效真名 OR 用户显式刷新）
AND 该 local_id 无 in-flight 请求
AND 未处于失败退避窗口
```

**尚无有效真名**：`immich_person_id` 空，或 `display_name` 为空 / 等于 `#<id>`。

因此：设备上已有 `#1`、`#2` 但无真名时，再次对着 `#1` **也会**调 Immich（不只「新 enrolled」）。

会话/NVS 已有真名且未点刷新 → **不调** Immich。

显式刷新：`POST /api/face_refresh_name`（可带 `local_id=`，默认当前 primary）。

## 4. 临时 asset

- 默认 **`delete_asset=0`**：识别结束后**保留**临时图，便于在 Immich 核对检测/识别。
- 设为 `1` 时：成功 / unknown / 超时均 DELETE（旧行为）；删除失败只打日志。
- 编译默认：`DEEP_DOG_FACE_IMMICH_DELETE_ASSET=0`；NVS 键 `del_asset`。

## 5. 结果与 UI

```json
{
  "local_id": 2,
  "display_name": "葛维冬",
  "recognize_source": "session"
}
```

- 成功：写 NVS（`display_name`、`immich_person_id`）；`/api/face` 显示真名。  
- unknown/error：保留数字 ID；退避，不覆盖有效真名缓存。

Key：NVS（`fdog_im`）；禁止 git 明文。

配置：

- `POST /api/immich_config?api_key=...&api_url=...&delete_asset=0|1`
- 可只发 `delete_asset=0|1`（已有 Key 时）
- `GET /api/immich_status` 含 `delete_asset`

默认 URL：`http://192.168.31.25:2283/api`。

## 6. 功能需求

| ID | 需求 |
|----|------|
| NAM-01 | 按 §3 门控调用 Immich（含已有无真名的 local_id） |
| NAM-02 | 异步不阻塞检测/MJPEG/狗控 |
| NAM-03 | 成功绑定 local_id ↔ 真名 |
| NAM-04 | 失败降级为数字 ID |
| NAM-05 | 可观测（upload/poll/result/可选 delete 日志） |
| NAM-06 | **不主动触发** Immich face jobs；只上传 + 轮询 |
| NAM-07 | 临时 asset 删除可配；**默认保留** |

## 7. 实现前健康检查

1. `GET http://192.168.31.25:2283/api/server/ping` → 200  
2. 带可写 Key：`GET /users/me` → 200  
3. 用 [fixtures/ge_weidong.png](../fixtures/ge_weidong.png)（人物 **葛维冬**）upload → poll `people` → 应得「葛维冬」（主机探针**不要** `PUT /jobs`）

若 Key 无效或库中无人脸命名，先修好 Immich 再改固件。

## 8. 验收

- [ ] Immich 已命名人物：可得正确真名（允许数秒延迟）；fixture 葛维冬  
- [ ] 已有 `#1` 无真名时再次出镜：会触发 Immich，最终显示真名  
- [ ] Immich 宕机 / 无 Key：仍显示 S04 数字 ID，推流/检测可用  
- [ ] 同人 ≥5s：Immich 调用符合去重（有真名后不再狂刷）  
- [ ] 默认不删临时 asset；`delete_asset=1` 时可删  
- [ ] 串口无 `job faceDetection` / `facialRecognition` 主动触发日志  
- [ ] 仓库无 API Key 明文  

> **已知局限（S06 应对）**：设备 **240×240** 上传 Immich 时经常 `people=[]`；主机清晰 [`ge_weidong.png`](../fixtures/ge_weidong.png) 可识别。**下一增量**：S06 §9 — OV2640 **640×480 采集** + Immich crop≥320，RTSP **320×240** 推流降级（评估可行，待实现）。见 [S06 §9](./S06-higher-resolution.md)。

## 9. 不包含

DeepDiary 后台中转主路径；Kiosk 正文（[P01](../product/P01-kiosk-personalization.md)）。分辨率提升见 **S06**。

## 10. 时间轴与 GPS

| 阶段 | 做法 | 效果 |
|------|------|------|
| **A** | multipart `fileCreatedAt` / `fileModifiedAt` = SNTP 同步后的 UTC ISO8601 | Immich 时间轴按真实拍摄/upload 分散 |
| **B** | MQTT `set_immich_config` 可选 `latitude`/`longitude` 写入 NVS；upload 成功后 `PUT /assets/{id}` | 地图/地点视图有 pin |

- 禁止写死 `2026-01-01`（会导致时间轴堆叠）。
- 不推荐公网 IP geolocation（LAN 精度差、隐私与延迟）。
