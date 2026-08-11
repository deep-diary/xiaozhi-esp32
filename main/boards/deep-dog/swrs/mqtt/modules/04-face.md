# 04 · face（人脸 / 人物）

| 项 | 内容 |
|----|------|
| module_id | `face` |
| capabilities | `face`（入口卡；含原 person 能力） |
| 路由建议 | `/device/:deviceId/modules/face` |
| 契约 | ready；`face/registry` retain；`person/active` ready |
| YAML | `face/cmd`、`face/status`、`face/registry`；`person/active` |
| 说明 | **实时框/跟踪 UI 在 [02-stream](./02-stream.md)**；本页做人脸开关、清库、间隔、Immich 轮播与打招呼 |

## 入口卡文案

- 标题：人脸  
- 说明：检测开关 / 识别 / Immich 轮播  
- **不出**独立 Track 卡、**不出**独立 Person 卡（Person 并入本页）

## 详情页目标

| 分区 | 内容 |
|------|------|
| **检测控制** | `enabled` / `recognize_enabled` / `pipeline` / `detect_interval_ms`；`action=clear_db` |
| **人脸库** | 订 `face/registry`；rename / delete_one / merge |
| **当前身份** | `faces[]`、`display_name`；可选订 `person/active` |
| **Immich / 打招呼** | 按身份拉相册轮播；节流打招呼（见下） |
| **实时画面** | 不嵌主直播；链到 Stream 页。预留日后 `face/thumb` |

兼容：`/modules/person` → redirect → `/modules/face`；`/modules/track` → `/modules/stream#track`。

## 与推流

固件 `DEEP_DOG_FACE_AI_DURING_RTSP=1`：**可与 RTSP 推流同时检测**。Stream 页订 `face/status` 画框即可。

识别依赖检测出框；**不存在**「关检测、只识别」。省电/静默身份用 `pipeline=identity`（检测间隔=识别间隔）。

## Topic

| Topic | 方向 | QoS | retain | 说明 |
|-------|------|-----|--------|------|
| `…/face/status` | ↑ | 0 | false | on_change 实时框 + **≈30s 控制态心跳**（非 retain，晚订阅靠心跳/`refresh_status`） |
| `…/face/registry` | ↑ | 0 | **true** | 已注册 canonical 人脸库 |
| `…/face/immich/status` | ↑ | 0 | **true** | Immich 配置 + 探活（见 [04-face-immich-mqtt](./04-face-immich-mqtt.md)） |
| `…/face/cmd` | ↓ | 1 | false | 见字段表 |
| `…/person/active` | ↑ | 0 | true | primary 身份变化 |
| `…/face/thumb` | ↑ | 0 | false | **planned**，默认不发 |
| `…/track/*` | — | — | — | UI 在 Stream 页 |

前缀：`deepdiary/deep-dog/{device_id}/`。字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 字段表 · `face/cmd`

兼容旧报文：仅 `{ "enabled": bool, "ts" }`。

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `enabled` | bool | 否* | 检测总开关 |
| `recognize_enabled` | bool | 否 | 识别开关；false 时只检测不 enroll |
| `action` | enum | 否 | `clear_db` \| `rename` \| `delete_one` \| `merge` \| `refresh_immich` \| `set_immich_config` \| `ping_immich` \| **`refresh_status`** |
| `local_id` | int | 否 | rename/delete/merge/refresh 目标 |
| `target_local_id` | int | 否 | merge 的 canonical id |
| `display_name` | string | 否 | rename 新名称 |
| `api_url` | string | 否 | Immich API 根（`set_immich_config` 或稀疏更新） |
| `api_key` | string | 否 | Immich Key（写入 NVS；status 仅回 `key_len`） |
| `delete_asset` | int | 否 | 0 保留临时图 / 1 识别后删除 |
| `pipeline` | enum | 否 | `live` \| `identity` |
| `detect_interval_ms` | int | 否 | **200–5000**；越界夹紧；status 回显生效值 |
| `ts` | int | 否 | Unix 秒 |

\* 至少带 `enabled` 或 `action` 或配置字段之一。

| pipeline | 行为 |
|----------|------|
| `live` | 按 `detect_interval_ms` 检测（可偏快）；识别按固件 recog 间隔（默认约 2s） |
| `identity` | 检测与识别**同一** `detect_interval_ms` |

跟踪开关用独立 [`track/cmd`](./05-track.md)，不进 `face/cmd`。

## 字段表 · `face/status`

| 字段 | 类型 | 说明 |
|------|------|------|
| `enabled` | bool | 总开关 |
| `recognize_enabled` | bool | 识别开关 |
| `pipeline` | string | `live` / `identity` |
| `detect_interval_ms` | int | 当前生效间隔 |
| `has_person` / `n` / `w` / `h` | — | 检测摘要；坐标为**像素** |
| `primary` / `faces[]` | — | 框 + `local_id` / `display_name` |
| `ts` | int | Unix 秒 |

**晚订阅**：`retain=false`，MQTT 重连时会 force 发一帧；之后检测关且画面不变时 fingerprint 去重会跳过。**固件每 ≈30s 强制发一帧**（含 `enabled`/`recognize_enabled` 等控制态，框可为空）。前端 mount 时可发 `face/cmd {"action":"refresh_status"}` 立即拉一帧。

## 样例 · `face/cmd`

**开/关（兼容）**

```json
{ "enabled": true, "ts": 1710000000 }
```

```json
{ "enabled": false, "ts": 1710000000 }
```

**直播叠加（约 3fps 检测）**

```json
{
  "enabled": true,
  "pipeline": "live",
  "detect_interval_ms": 500,
  "ts": 1710000000
}
```

**静默身份（检测=识别，例如 2s）**

```json
{
  "enabled": true,
  "pipeline": "identity",
  "detect_interval_ms": 2000,
  "ts": 1710000000
}
```

**改间隔（1～5s 档）**

```json
{ "detect_interval_ms": 1000, "ts": 1710000000 }
```

```json
{ "detect_interval_ms": 3000, "ts": 1710000000 }
```

**alias 合并（侧脸→正脸，保留 embedding）**

```json
{ "action": "merge", "local_id": 3, "target_local_id": 2, "ts": 1710000000 }
```

**重命名**

```json
{ "action": "rename", "local_id": 2, "display_name": "张三", "ts": 1710000000 }
```

**清空已识别人脸（重测）**

```json
{ "action": "clear_db", "ts": 1710000000 }
```

拍照视觉解释已迁到 [02-stream](./02-stream.md)（`stream/cmd` `take_photo` → `stream/photo`），不在本模块。

## 样例 · `face/status`

```json
{
  "enabled": true,
  "pipeline": "live",
  "detect_interval_ms": 333,
  "has_person": true,
  "n": 1,
  "w": 240,
  "h": 240,
  "primary": { "cx": 120, "cy": 100, "score": 0.9 },
  "faces": [
    {
      "x0": 80, "y0": 60, "x1": 160, "y1": 140,
      "cx": 120, "cy": 100, "score": 0.9,
      "local_id": 2, "display_name": "张三"
    }
  ],
  "ts": 1710000000
}
```

## 样例 · `face/registry`

```json
{
  "version": 1,
  "count": 1,
  "entries": [
    {
      "local_id": 2,
      "display_name": "张三",
      "immich_person_id": "uuid",
      "immich_asset_id": "asset-uuid",
      "aliases": [3],
      "updated_at": 1710000000
    }
  ],
  "ts": 1710000000
}
```

## `person/active`

```json
{ "local_id": 2, "display_name": "张三", "immich_person_id": "uuid-optional", "ts": 1710000000 }
```

无人时可不发或发空身份。打招呼：前端/对话服务对「已知人」做 cooldown（如 300–1800s），设备只保证身份事件。

## `face/thumb`（planned，本期不实现）

事件触发 JPEG base64 小图；默认关。见计划评估：优先新人脸 1 张，禁止按检测频率刷整帧。

## Steps（前端）

- **Step 1** `capabilities.face`；无独立 Person/Track 卡。
- **Step 2** 订 `face/status`；可选 `person/active`。
- **Step 3** 控件：enable、pipeline、interval、clear_db（确认框）。
- **Step 4** Immich 轮播 + 节流打招呼；实时看流 → Stream 页。
- **Step 5** unmount 退订。

## 固件要点

- `DURING_RTSP=1`；`clear_db` 清 facedb+NVS+session。
- `live` / `identity` 分频；`detect_interval_ms` 运行时夹紧 200–5000。
- **`face/registry` retain 与 `face/status` 语义分离**：status 为实时框；registry 为 canonical 已注册库。
- **boot 默认关 Face AI**（`DEEP_DOG_FACE_AI_DEFAULT_ENABLED=0`）时，MQTT 首连可能先于 recognizer 就绪而 retain 空库；**recognizer 从 NVS/facedb 恢复后必须 republish registry**（覆盖陈旧 retain）。
- **`face/cmd` lazy-start**：先 `xQueueCreate` + `dog_face_ai` task（占栈），再加载检测/识别模型；避免 RTSP 推流时 internal 碎片化导致 `xTaskCreate` 失败。启动在 `face_boot` 任务异步完成；**不得**在 `mqtt_task` 上同步加载模型。
- **`face/cmd` 变更检测/识别开关成功后**，须 republish registry（`esp_timer` 异步 publish）。

## 验收

- [ ] 推流中 `has_person` 可为 true
- [ ] `clear_db` 后身份清空可重测
- [ ] `pipeline` / 间隔在 status 回显
- [ ] 入口无独立 Person 卡；Track UI 在 Stream
- [ ] 有 enrolled 库的设备：`face/registry` retain `count≥1`，entries 与 `face/status` 识别 id/名称一致
- [ ] 冷启动 Face AI off→MQTT cmd `enabled:true` 后 registry 从 0 变为完整库
- [ ] 晚订阅 Stream/Face 页：≤30s 内收到 `face/status` 控制态，或 `refresh_status` 立即回一帧
